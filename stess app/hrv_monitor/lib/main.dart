import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';

enum StressWindow {
  lastMinute,
  last5Minutes,
  last15Minutes,
  allData,
}

class StressSample {
  StressSample({
    required this.timestamp,
    required this.bpm,
    required this.stress,
    required this.fingerOnSensor,
  });

  final DateTime timestamp;
  final int bpm;
  final int stress;
  final bool fingerOnSensor;
}

class StressSummary {
  StressSummary({
    required this.window,
    required this.sampleCount,
    required this.averageBpm,
    required this.averageStress,
    required this.minStress,
    required this.maxStress,
    required this.trend,
  });

  final StressWindow window;
  final int sampleCount;
  final double averageBpm;
  final double averageStress;
  final int minStress;
  final int maxStress;
  final String trend;
}

String stressWindowLabel(StressWindow window) {
  switch (window) {
    case StressWindow.lastMinute:
      return 'Last 1 min';
    case StressWindow.last5Minutes:
      return 'Last 5 min';
    case StressWindow.last15Minutes:
      return 'Last 15 min';
    case StressWindow.allData:
      return 'All data';
  }
}

Duration? stressWindowDuration(StressWindow window) {
  switch (window) {
    case StressWindow.lastMinute:
      return const Duration(minutes: 1);
    case StressWindow.last5Minutes:
      return const Duration(minutes: 5);
    case StressWindow.last15Minutes:
      return const Duration(minutes: 15);
    case StressWindow.allData:
      return null;
  }
}

StressSummary? analyzeStressSamples(
  List<StressSample> samples,
  StressWindow window, {
  DateTime? now,
}) {
  final effectiveNow = now ?? DateTime.now();
  final duration = stressWindowDuration(window);
  final filtered = samples.where((sample) {
    if (!sample.fingerOnSensor) return false;
    if (duration == null) return true;
    return sample.timestamp.isAfter(effectiveNow.subtract(duration));
  }).toList()
    ..sort((left, right) => left.timestamp.compareTo(right.timestamp));

  if (filtered.isEmpty) {
    return null;
  }

  final sampleCount = filtered.length;
  final averageBpm = filtered.map((sample) => sample.bpm).reduce((a, b) => a + b) /
      sampleCount;
  final averageStress = filtered.map((sample) => sample.stress).reduce((a, b) => a + b) /
      sampleCount;
  final minStress = filtered.map((sample) => sample.stress).reduce((a, b) => a < b ? a : b);
  final maxStress = filtered.map((sample) => sample.stress).reduce((a, b) => a > b ? a : b);

  final firstHalfSize = (sampleCount / 2).ceil();
  final firstHalfAverage = filtered
          .take(firstHalfSize)
          .map((sample) => sample.stress)
          .reduce((a, b) => a + b) /
      firstHalfSize;
  final secondHalfSamples = filtered.skip(sampleCount - firstHalfSize).toList();
  final secondHalfAverage = secondHalfSamples
          .map((sample) => sample.stress)
          .reduce((a, b) => a + b) /
      secondHalfSamples.length;
  final delta = secondHalfAverage - firstHalfAverage;

  final trend = delta > 3
      ? 'Rising'
      : delta < -3
          ? 'Improving'
          : 'Stable';

  return StressSummary(
    window: window,
    sampleCount: sampleCount,
    averageBpm: averageBpm,
    averageStress: averageStress,
    minStress: minStress,
    maxStress: maxStress,
    trend: trend,
  );
}

void main() => runApp(const HRVApp());

class HRVApp extends StatelessWidget {
  const HRVApp({super.key});
  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'HRV Monitor',
      theme: ThemeData.dark(),
      home: const HRVScreen(),
    );
  }
}

class HRVScreen extends StatefulWidget {
  const HRVScreen({super.key});
  @override
  State<HRVScreen> createState() => _HRVScreenState();
}

class _HRVScreenState extends State<HRVScreen> {
  BluetoothDevice?         _device;
  int    _bpm       = 0;
  int    _stress    = 0;
  String _state     = 'UNKNOWN';
  bool   _connected = false;
  bool   _finger    = false;
  String _buffer    = '';
  final List<StressSample> _samples = [];
  StressWindow _selectedWindow = StressWindow.last5Minutes;

  @override
  void initState() {
    super.initState();
    _waitForBluetooth();
  }

  void _waitForBluetooth() {
    FlutterBluePlus.adapterState.listen((state) {
      if (state == BluetoothAdapterState.on) {
        _startScan();
      } else if (state == BluetoothAdapterState.unauthorized) {
        debugPrint('Bluetooth permission denied');
      }
    });
  }

  void _startScan() {
    FlutterBluePlus.startScan(timeout: const Duration(seconds: 10));
    FlutterBluePlus.scanResults.listen((results) {
      for (ScanResult r in results) {
        if (r.device.platformName.contains('HMSoft')) {
          FlutterBluePlus.stopScan();
          _connect(r.device);
          break;
        }
      }
    });
  }

  Future<void> _connect(BluetoothDevice device) async {
    _device = device;
    await device.connect();
    setState(() => _connected = true);

    List<BluetoothService> services = await device.discoverServices();
    for (BluetoothService s in services) {
      // Target HM-10 specific service FFE0
      if (s.uuid.toString().toUpperCase().contains('FFE0')) {
        for (BluetoothCharacteristic c in s.characteristics) {
          if (c.uuid.toString().toUpperCase().contains('FFE1')) {
            await c.setNotifyValue(true);
            c.onValueReceived.listen(_onData);
            debugPrint('HM-10 FFE1 characteristic subscribed');
          }
        }
      }
    }
  }

  void _onData(List<int> data) {
    final raw = utf8.decode(data);
    debugPrint('RAW: $raw');
    _buffer += raw;
    while (_buffer.contains('\n')) {
      int    idx  = _buffer.indexOf('\n');
      String line = _buffer.substring(0, idx).trim();
      _buffer     = _buffer.substring(idx + 1);
      debugPrint('LINE: $line');
      _parseLine(line);
    }
  }

  void _parseLine(String line) {
    if (!line.startsWith('\$HRV,')) return;
    final parts = line.substring(5).split(',');
    if (parts.length < 3) return;

    final bpm    = int.tryParse(parts[0]) ?? 0;
    final stress = int.tryParse(parts[1]) ?? 0;
    final finger = parts[2].trim() == '1';

    String state;
    if      (stress > 70) {
      state = 'HIGH STRESS';
    } else if (stress > 40) {
      state = 'MEDIUM';
    } else if (stress > 20) {
      state = 'RELAXED';
    } else {
      state = 'VERY RELAXED';
    }

    final sample = StressSample(
      timestamp: DateTime.now(),
      bpm: bpm,
      stress: stress,
      fingerOnSensor: finger,
    );

    setState(() {
      _bpm    = bpm;
      _stress = stress;
      _state  = state;
      _finger = finger;
      if (finger) {
        _samples.add(sample);
        if (_samples.length > 2000) {
          _samples.removeAt(0);
        }
      }
    });
  }

  Color _stressColor() {
    if (_stress > 70) return Colors.redAccent;
    if (_stress > 40) return Colors.yellowAccent;
    return Colors.greenAccent;
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: const Color(0xFF111111),
      appBar: AppBar(
        backgroundColor: const Color(0xFF1E1E1E),
        title: const Text('HRV Monitor'),
        actions: [
          Icon(
            _connected ? Icons.bluetooth_connected : Icons.bluetooth_disabled,
            color: _connected ? Colors.blueAccent : Colors.grey,
          ),
          const SizedBox(width: 16),
        ],
      ),
      body: _connected ? _dashboard() : _scanning(),
    );
  }

  Widget _scanning() {
    return Center(
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          const CircularProgressIndicator(color: Colors.cyanAccent),
          const SizedBox(height: 16),
          const Text(
            'Scanning for HM-10...',
            style: TextStyle(color: Colors.white),
          ),
          const SizedBox(height: 8),
          const Text(
            'Make sure Bluetooth is enabled',
            style: TextStyle(color: Colors.grey, fontSize: 12),
          ),
          const SizedBox(height: 24),
          ElevatedButton(
            onPressed: _waitForBluetooth,
            child: const Text('Retry'),
          ),
        ],
      ),
    );
  }

  Widget _dashboard() {
    return SingleChildScrollView(
      padding: const EdgeInsets.all(24),
      child: Column(
        children: [
          // Finger status banner
          if (!_finger)
            Container(
              width: double.infinity,
              padding: const EdgeInsets.all(12),
              margin: const EdgeInsets.only(bottom: 16),
              decoration: BoxDecoration(
                color: Colors.orange.withValues(alpha: 0.2),
                borderRadius: BorderRadius.circular(12),
                border: Border.all(color: Colors.orange),
              ),
              child: const Row(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  Icon(Icons.warning_amber, color: Colors.orange, size: 18),
                  SizedBox(width: 8),
                  Text(
                    'PLACE FINGER ON SENSOR',
                    style: TextStyle(color: Colors.orange, fontSize: 13),
                  ),
                ],
              ),
            ),

          // BPM Card
          _card(
            label: 'HEART RATE',
            value: _finger ? '$_bpm' : '--',
            unit: 'BPM',
            color: Colors.cyanAccent,
          ),
          const SizedBox(height: 16),

          // Stress Card
          _card(
            label: 'STRESS INDEX',
            value: _finger ? '$_stress' : '--',
            unit: '/ 100',
            color: _stressColor(),
          ),
          const SizedBox(height: 16),

          Container(
            width: double.infinity,
            padding: const EdgeInsets.all(20),
            decoration: BoxDecoration(
              color: const Color(0xFF1E1E1E),
              borderRadius: BorderRadius.circular(16),
            ),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                const Text(
                  'STRESS ANALYSIS',
                  style: TextStyle(
                    color: Colors.grey,
                    fontSize: 12,
                    letterSpacing: 1.2,
                  ),
                ),
                const SizedBox(height: 12),
                Wrap(
                  spacing: 8,
                  runSpacing: 8,
                  children: StressWindow.values.map((window) {
                    final selected = _selectedWindow == window;
                    return ChoiceChip(
                      label: Text(stressWindowLabel(window)),
                      selected: selected,
                      onSelected: (_) {
                        setState(() => _selectedWindow = window);
                      },
                      selectedColor: Colors.cyanAccent.withValues(alpha: 0.18),
                      labelStyle: TextStyle(
                        color: selected ? Colors.cyanAccent : Colors.white70,
                        fontWeight: FontWeight.w600,
                      ),
                      backgroundColor: const Color(0xFF2A2A2A),
                      side: BorderSide(
                        color: selected ? Colors.cyanAccent : Colors.transparent,
                      ),
                    );
                  }).toList(),
                ),
                const SizedBox(height: 16),
                Builder(
                  builder: (context) {
                    final summary = analyzeStressSamples(
                      _samples,
                      _selectedWindow,
                    );

                    if (summary == null) {
                      return const Text(
                        'No valid samples collected yet for the selected period.',
                        style: TextStyle(color: Colors.white54, fontSize: 14),
                      );
                    }

                    return Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Text(
                          '${summary.trend} over ${stressWindowLabel(summary.window)}',
                          style: TextStyle(
                            color: summary.trend == 'Improving'
                                ? Colors.greenAccent
                                : summary.trend == 'Rising'
                                    ? Colors.orangeAccent
                                    : Colors.cyanAccent,
                            fontSize: 20,
                            fontWeight: FontWeight.bold,
                          ),
                        ),
                        const SizedBox(height: 12),
                        Row(
                          children: [
                            Expanded(
                              child: _analysisStat(
                                'Avg stress',
                                summary.averageStress.toStringAsFixed(1),
                              ),
                            ),
                            const SizedBox(width: 12),
                            Expanded(
                              child: _analysisStat(
                                'Avg BPM',
                                summary.averageBpm.toStringAsFixed(1),
                              ),
                            ),
                          ],
                        ),
                        const SizedBox(height: 12),
                        Row(
                          children: [
                            Expanded(
                              child: _analysisStat(
                                'Min / Max',
                                '${summary.minStress} / ${summary.maxStress}',
                              ),
                            ),
                            const SizedBox(width: 12),
                            Expanded(
                              child: _analysisStat(
                                'Samples',
                                '${summary.sampleCount}',
                              ),
                            ),
                          ],
                        ),
                      ],
                    );
                  },
                ),
              ],
            ),
          ),
          const SizedBox(height: 16),

          // State Card
          Container(
            width: double.infinity,
            padding: const EdgeInsets.all(24),
            decoration: BoxDecoration(
              color: const Color(0xFF1E1E1E),
              borderRadius: BorderRadius.circular(16),
            ),
            child: Column(
              children: [
                Text(
                  _finger ? _state : 'NO FINGER',
                  style: TextStyle(
                    fontSize: 28,
                    fontWeight: FontWeight.bold,
                    color: _finger ? _stressColor() : Colors.grey,
                  ),
                ),
                const SizedBox(height: 8),
                const Text(
                  'CURRENT STATE',
                  style: TextStyle(color: Colors.grey, fontSize: 12),
                ),
              ],
            ),
          ),
          const SizedBox(height: 16),

          // Device info card
          Container(
            width: double.infinity,
            padding: const EdgeInsets.all(16),
            decoration: BoxDecoration(
              color: const Color(0xFF1E1E1E),
              borderRadius: BorderRadius.circular(16),
            ),
            child: Row(
              mainAxisAlignment: MainAxisAlignment.spaceBetween,
              children: [
                const Text(
                  'DEVICE',
                  style: TextStyle(color: Colors.grey, fontSize: 12),
                ),
                Text(
                  _device?.platformName ?? 'Unknown',
                  style: const TextStyle(
                    color: Colors.blueAccent,
                    fontSize: 12,
                    fontWeight: FontWeight.bold,
                  ),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }

  Widget _card({
    required String label,
    required String value,
    required String unit,
    required Color  color,
  }) {
    return Container(
      width: double.infinity,
      padding: const EdgeInsets.all(24),
      decoration: BoxDecoration(
        color: const Color(0xFF1E1E1E),
        borderRadius: BorderRadius.circular(16),
      ),
      child: Column(
        children: [
          Row(
            mainAxisAlignment: MainAxisAlignment.center,
            crossAxisAlignment: CrossAxisAlignment.end,
            children: [
              Text(
                value,
                style: TextStyle(
                  fontSize: 64,
                  fontWeight: FontWeight.bold,
                  color: color,
                ),
              ),
              Padding(
                padding: const EdgeInsets.only(bottom: 12, left: 8),
                child: Text(
                  unit,
                  style: const TextStyle(color: Colors.grey, fontSize: 16),
                ),
              ),
            ],
          ),
          Text(
            label,
            style: const TextStyle(color: Colors.grey, fontSize: 12),
          ),
        ],
      ),
    );
  }

  Widget _analysisStat(String label, String value) {
    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: const Color(0xFF151515),
        borderRadius: BorderRadius.circular(12),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(
            label,
            style: const TextStyle(color: Colors.white54, fontSize: 12),
          ),
          const SizedBox(height: 8),
          Text(
            value,
            style: const TextStyle(
              color: Colors.white,
              fontSize: 20,
              fontWeight: FontWeight.bold,
            ),
          ),
        ],
      ),
    );
  }

  @override
  void dispose() {
    _device?.disconnect();
    super.dispose();
  }
}

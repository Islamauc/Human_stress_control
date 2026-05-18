import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';

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
  BluetoothCharacteristic? _char;
  int    _bpm       = 0;
  int    _stress    = 0;
  String _state     = 'UNKNOWN';
  bool   _connected = false;
  bool   _finger    = false;
  String _buffer    = '';

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
            _char = c;
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
    if      (stress > 70) state = 'HIGH STRESS';
    else if (stress > 40) state = 'MEDIUM';
    else if (stress > 20) state = 'RELAXED';
    else                  state = 'VERY RELAXED';

    setState(() {
      _bpm    = bpm;
      _stress = stress;
      _state  = state;
      _finger = finger;
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
                color: Colors.orange.withOpacity(0.2),
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

  @override
  void dispose() {
    _device?.disconnect();
    super.dispose();
  }
}
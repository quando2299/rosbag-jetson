import 'dart:async';
import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter_webrtc/flutter_webrtc.dart';
import 'package:get_it/get_it.dart';
import 'package:robot_simulator/mqtt_test/mqtt_test_screen.dart';
import 'package:robot_simulator/tractor_camera_mock/tractor_camera_mock_screen.dart';

import 'mqtt_service.dart';
import 'widgets/button.dart';
import 'widgets/textbox.dart';

final mqttService = locator<MqttService>();

GetIt locator = GetIt.instance;

void setUpInjector() {
  locator.registerLazySingleton(() => MqttService());
}

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();

  if (!kIsWeb) {
    await WebRTC.initialize();
  }

  setUpInjector();
  runApp(const MyApp());
}

class MyApp extends StatelessWidget {
  const MyApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        colorScheme: ColorScheme.fromSeed(seedColor: const Color(0xFF004F98)),
        useMaterial3: true,
      ),
      home: const MyHomePage(),
    );
  }
}

class MyHomePage extends StatefulWidget {
  const MyHomePage({super.key});

  @override
  State<MyHomePage> createState() => _MyHomePageState();
}

class _MyHomePageState extends State<MyHomePage> {
  late TextEditingController _thingNameCtr;

  bool _enable = false;
  bool _isLoading = false;

  @override
  void initState() {
    _thingNameCtr = TextEditingController()
      ..addListener(() {
        setState(() {
          _enable = _thingNameCtr.text.isNotEmpty;
        });
      });
    super.initState();
  }

  void setLoading(bool isLoading) {
    setState(() {
      _isLoading = isLoading;
    });
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: ValueListenableBuilder(
          valueListenable: mqttService.connectState,
          builder: (context, connectState, widget) {
            return Center(
              child: SizedBox(
                width: MediaQuery.sizeOf(context).width / 1.5,
                child: Column(
                  mainAxisAlignment: MainAxisAlignment.center,
                  children: <Widget>[
                    Textbox(
                      controller: _thingNameCtr,
                      hintText: 'ThingName',
                      enabled: false,
                    ),
                    const SizedBox(height: 10),
                    Button(
                      title: 'Choose MQTT Information',
                      isLoading:
                          _isLoading && connectState != ConnectState.connected,
                      onPressed: () async {
                        setLoading(true);
                        mqttService.onDisconnected();
                        _thingNameCtr.text =
                            await mqttService.pickFileCer(context);

                        if (_thingNameCtr.text.isEmpty) {
                          setLoading(false);
                        }
                      },
                    ),
                    const SizedBox(height: 10),
                    if (connectState == ConnectState.connecting)
                      const Text('MQTT connecting...'),
                    const SizedBox(height: 10),
                    if (!kIsWeb && Platform.isAndroid)
                      Button(
                        title: 'Open Robot Camera',
                        enable: _enable &&
                            connectState == ConnectState.connected &&
                            Platform.isAndroid,
                        onPressed: () async {
                          Navigator.of(context).push(
                            MaterialPageRoute(
                              builder: (c) => TractorCameraScreen(
                                thingName: _thingNameCtr.text,
                              ),
                            ),
                          );
                        },
                      ),
                    const SizedBox(height: 10),
                    Button(
                      title: 'MQTT Testing',
                      enable: _enable && connectState == ConnectState.connected,
                      onPressed: () async {
                        Navigator.of(context).push(
                          MaterialPageRoute(
                            builder: (c) => MqttTestScreen(
                              thingName: _thingNameCtr.text,
                            ),
                          ),
                        );
                      },
                    ),
                  ],
                ),
              ),
            );
          }),
    );
  }
}

// Project: Robot simulator
// Copyright (c) 2025 M2M craft Co., Ltd.

import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'package:file_picker/file_picker.dart';
import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:mqtt5_client/mqtt5_client.dart';
import 'package:mqtt5_client/mqtt5_server_client.dart';
import 'package:robot_simulator/widgets/toast.dart';
import 'package:path/path.dart' as p;

enum ConnectState {
  connecting,
  connected,
  disconnected,
}

class MqttService {
  String server = "";
  int port = 1883;
  String thingname = "";
  String username = "";
  String password = "";
  int tryConnect = 0;

  MqttServerClient? client;
  StreamSubscription<dynamic>? streamSubs;
  Set<String> subscribedTopics = {};

  ValueNotifier<ConnectState> connectState =
      ValueNotifier(ConnectState.disconnected);

  final StreamController<(String?, String)> _streamMessageController =
      StreamController<(String?, String)>.broadcast();
  Stream<(String?, String)> get streamMessage =>
      _streamMessageController.stream;

  String getThingName(dynamic topic) {
    return topic.toString().split('/')[0];
  }

  Future<String> pickFileCer(BuildContext context) async {
    tryConnect = 0;
    final fileResults = await FilePicker.platform.pickFiles(
      type: FileType.any,
      allowMultiple: false,
      withData: true,
    );
    try {
      if (fileResults == null) return '';
      if (fileResults.files.isEmpty) return '';

      if (p.extension(fileResults.files.single.name) != '.csv') {
        showToast('MQTT file is incorrect', context, isError: true);
        return '';
      }

      String csvString = '';
      if (kIsWeb) {
        csvString = utf8.decode(fileResults.files.single.bytes!);
      } else {
        final file = File(fileResults.files.single.path!);
        csvString = await file.readAsString();
      }

      final cleaned = csvString.replaceAll('"', '');
      final parts = cleaned.split(',');
      if (parts.length != 4 || int.tryParse(parts[1]) == null) {
        showToast('MQTT file is incorrect', context, isError: true);
        return '';
      }
      server = parts[0];
      port = int.tryParse(parts[1])!;
      username = parts[2];
      thingname = parts[2];
      password = parts[3];

      await connectWithCertificate();
      return thingname;
    } catch (e) {
      showToast(e.toString(), context, isError: true);
    }

    return '';
  }

  Future<void> connectWithCertificate() async {
    if (tryConnect > 5) {
      tryConnect = 0;
      thingname = '';
      return;
    }
    client =
    MqttServerClient.withPort(server, thingname, port)
      ..useWebSocket = false
      ..secure = false
      ..logging(on: false)
      ..keepAlivePeriod = 30
      ..onDisconnected = onDisconnected
      ..onConnected = onConnected
      ..autoReconnect = true
      ..connectionMessage =
          MqttConnectMessage().withClientIdentifier(thingname);

    print('MQTT::Mosquitto client connecting....');
    try {
      await client?.connect(username, password);
    } on Exception catch (e) {
      print('MQTT::Connect error $e');
      sleep(const Duration(seconds: 1));

      tryConnect += 1;
      await connectWithCertificate();
    }
  }

  /// The function subscribe message
  void onSubs({
    required List<String> topics,
    MqttQos qos = MqttQos.atLeastOnce,
  }) {
    final connectionStatus = client?.connectionStatus?.state;
    if (connectionStatus != null &&
        connectionStatus == MqttConnectionState.connected) {
      for (var i = 0; i < topics.length; i++) {
        final topic = topics[i];
        client?.unsubscribeStringTopic(topic);
        client?.subscribe(topic, qos);
        subscribedTopics.add(topic);
        print('MQTT::Subscribe topic: $topic ');
      }
    } else {
      connectWithCertificate();
    }
  }

  void unSubscribeAll() {
    for (final topic in subscribedTopics) {
      client?.unsubscribeStringTopic(topic);
      print('MQTT::Unsubscribe topic: $topic ');
    }
    subscribedTopics.clear();
  }

  /// The function to unsubscribe specified topics
  void unSubscribe(List<String> topics) {
    for (final topic in topics) {
      client?.unsubscribeStringTopic(topic);
      debugPrint('UNSUBSCRIBE: Unsubscribe topic: $topic ');
      subscribedTopics.remove(topic);
    }
  }

  /// The function publish message
  void onPub({
    required String topic,
    required String message,
    MqttQos qos = MqttQos.atLeastOnce,
  }) {
    final builder = MqttPayloadBuilder()..addUTF8String(message);
    print('MQTT::Publish to topic: $topic ');
    final payload = builder.payload;
    if (payload != null) {
      client?.publishMessage(topic, qos, payload);
    }
  }

  void tryConnected() {
    connectState.value = ConnectState.connecting;
    connectWithCertificate();
  }

  /// The unsolicited disconnect callback
  Future<void> onDisconnected() async {
    final connectStatus = client?.connectionStatus?.state;
    if (connectStatus != null &&
        connectStatus == MqttConnectionState.connected) {
      client?.disconnect();
    }

    connectState.value = ConnectState.disconnected;
    print('MQTT::MQTT client disconnected and cleaned up.');
  }

  /// The successful connect callback
  void onConnected() {
    print('MQTT::Mosquitto client connected.');
    connectState.value = ConnectState.connected;

    //Cancel stream before create new stream
    streamSubs?.cancel();
    streamSubs = client?.updates.listen((messages) {
      for (final message in messages) {
        final payload = message.payload as MqttPublishMessage;
        final content =
            MqttUtilities.bytesToStringAsString(payload.payload.message!);
        _streamMessageController.sink.add((message.topic, content));
      }
    });
  }
}

// Project: Robot simulator
// Copyright (c) 2025 M2M craft Co., Ltd.

import 'dart:async';
import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:flutter_webrtc/flutter_webrtc.dart';
import 'package:mqtt5_client/mqtt5_client.dart';
import 'package:robot_simulator/main.dart';

import 'tractor_camera_mock_screen.dart';

/// MQTT topic for robot control.
const String robotControl = 'robot-control';

/// MQTT topic for WebRTC answers.
const String answerTopic = 'answer';

/// MQTT topic for robot ICE candidates.
const String robotCandidateTopic = 'candidate/robot';

/// MQTT topic for RMCS ICE candidates.
const String rmcsCandidateTopic = 'candidate/rmcs';

/// MQTT topic for WebRTC offers.
const String offerTopic = 'offer';

/// MQTT topic for client disconnect messages.
const String clientDisconnectTopic = 'disconnect-client';

/// MQTT topic for tractor disconnect messages.
const String tractorDisconnectTopic = 'disconnect-tractor';

/// Data channel label for RMCS timestamp.
const String rmcsTimestampChannel = 'RMCSTimestampChannel';

/// WebRTC configuration.
const kConfiguration = {
  'iceServers': [
    {'urls': 'stun:stun.l.google.com:19302'},
    {'urls': 'stun:stun1.l.google.com:19302'},
    {'urls': 'stun:stun2.l.google.com:19302'},
    {'urls': 'stun:stun3.l.google.com:19302'},
    {'urls': 'stun:stun4.l.google.com:19302'},
  ],
};

/// Maximum number of ICE candidates to store.
const kMaxCandidateStorage = 10;

/// Represents data for an RTCPeerConnection.
class RTCPeerData {
  /// The RTCPeerConnection instance.
  final RTCPeerConnection peerConnection;

  /// List of robot ICE candidates.
  final List<RTCIceCandidate> robotIceCandidates;

  /// Flag indicating if the remote description is set.
  bool isRemoteDescription;

  /// Creates an instance of [RTCPeerData].
  RTCPeerData({
    required this.peerConnection,
    required this.robotIceCandidates,
    required this.isRemoteDescription,
  });
}

/// Mixin for managing the state of the TractorCameraScreen.
///
/// This mixin handles WebRTC peer connections, MQTT communication,
/// and camera operations for the tractor camera mock screen.
mixin TractorCameraScreenState on State<TractorCameraScreen> {
  /// Base MQTT topic for the tractor.
  late String _baseTopic;

  /// Local video renderer for displaying the camera feed.
  late RTCVideoRenderer localVideoRenderer;

  /// Local media stream from the camera.
  late MediaStream _localStream;

  final Map<String, RTCPeerData?> _peers = {};
  bool enableSound = false;

  bool isTreeView = false;
  StreamSubscription? streamMessage;

  /// List of MQTT topics to subscribe to, along with their notification status
  /// and TextEditingControllers for displaying received data.
  final topics =
      Topic.values.map((e) => (e, false, TextEditingController())).toList();

  /// Initializes the state when the widget is first created.
  @override
  void initState() {
    super.initState();
    init();
  }

  @override
  void dispose() async {
    cleanUp();
    super.dispose();
  }

  /// Cleans up resources when the widget is disposed.
  Future<void> cleanUp() async {
    streamMessage?.cancel();
    // Close and dispose all peer connections.
    for (final e in _peers.entries) {
      await e.value?.peerConnection.close();
      await e.value?.peerConnection.dispose();
    }
    // Clear the map of peers.
    _peers.clear();
    // Detach the stream from the renderer.
    localVideoRenderer.srcObject = null;
    // Dispose the local video renderer.
    await localVideoRenderer.dispose();
    // Dispose the local media stream.
    await _localStream.dispose();

    // Unsubscribe from MQTT topics and publish a disconnect message.
    mqttService
      ..unSubscribe([
        '$_baseTopic/+/$offerTopic',
        '$_baseTopic/+/$robotCandidateTopic',
        '$_baseTopic/+/$clientDisconnectTopic',
        ...Topic.values.map((e) => getTopic(widget.thingName, e.value)),
      ])
      ..onPub(
        topic: '$_baseTopic/$tractorDisconnectTopic',
        message: 'disconnect',
        qos: MqttQos.atMostOnce,
      );
  }

  /// Initializes the component.
  ///
  /// Sets up the base MQTT topic, initializes the local video renderer,
  /// subscribes to MQTT topics, starts the camera, and listens for MQTT messages.
  void init() {
    _baseTopic = '${widget.thingName}/$robotControl';
    localVideoRenderer = RTCVideoRenderer();

    WidgetsBinding.instance.addPostFrameCallback((_) async {
      await localVideoRenderer.initialize();
      await _onSubTopics();
      await startCamera();

      streamMessage = mqttService.streamMessage.listen((data) async {
        final (topic, msg) = data;
        if (topic == null) {
          return;
        }

        if (Topic.values.any((e) => topic.contains(e.value))) {
          const encoder = JsonEncoder.withIndent('  ');
          final object = const JsonDecoder().convert(msg);
          final prettyString = encoder.convert(object);
          // Find the index of the topic that matches the received message.
          final index =
              topics.indexWhere((element) => topic.endsWith(element.$1.value));
          final (t, _, mqttData) = topics[index];
          // Update the text of the corresponding TextEditingController.
          mqttData.text = prettyString;
          // Update the state to reflect the new data.
          setState(() {
            topics[index] = (t, true, mqttData);
          });
        } else {
          await _handleMqttFromRMCS(topic, msg);
        }
      });
    });
  }

  /// Set the tree view state.
  void setTreeView() {
    setState(() {
      isTreeView = !isTreeView;
    });
  }

  /// Set the tab state.
  void setTab(int index) {
    if (index == 0) {
      return;
    }
    setState(() {
      final (t, isNotification, mqttData) = topics[index - 1];
      topics[index - 1] = (t, false, mqttData);
    });
  }

  /// Starts the camera and sets up the local video stream.
  Future<void> startCamera() async {
    _localStream = await navigator.mediaDevices.getUserMedia({
      'audio': false,
      'video': true,
    });
    localVideoRenderer.srcObject = _localStream;
    setState(() {});
  }

  /// Retrieves the topic string for a given thing name and sub-topic.
  String getTopic(String thingName, String subTopic) {
    return '$thingName/$subTopic';
  }

  /// Subscribes to MQTT topics.
  Future<void> _onSubTopics() async {
    mqttService.onSubs(
      topics: [
        '$_baseTopic/+/$offerTopic',
        '$_baseTopic/+/$robotCandidateTopic',
        '$_baseTopic/+/$clientDisconnectTopic',
      ].toList(),
      qos: MqttQos.atMostOnce,
    );

    mqttService.onSubs(
      topics:
          Topic.values.map((e) => getTopic(widget.thingName, e.value)).toList(),
    );
  }

  /// Disposes the peer connection for the given [peerId].
  Future<void> _disposePeerConnection(String peerId) async {
    if (_peers.containsKey(peerId)) {
      final peerConnection = _peers[peerId]!.peerConnection;
      await peerConnection.close();
      await peerConnection.dispose();
      _peers.remove(peerId);
    }
  }

  /// Sets up a new peer connection for the given [peerId].
  /// This includes creating the peer connection, adding local media tracks,
  /// and setting up event handlers for connection state changes and ICE candidates.
  Future<RTCPeerData> setupPeerConnection(String peerId) async {
    await _disposePeerConnection(peerId);

    final pc = await createPeerConnection(kConfiguration);

    final tracks = _localStream.getVideoTracks();
    for (final track in tracks) {
      if (track.id != null) {
        pc.addTrack(track, _localStream);
      }
    }

    _peers[peerId] = RTCPeerData(
      peerConnection: pc,
      robotIceCandidates: [],
      isRemoteDescription: false,
    );

    pc
      ..onConnectionState = (state) async {
        if (state == RTCPeerConnectionState.RTCPeerConnectionStateClosed ||
            state == RTCPeerConnectionState.RTCPeerConnectionStateFailed) {
          await _disposePeerConnection(peerId);
        }
      }
      ..onIceCandidate = (candidate) => onIceCandidate(candidate, peerId);

    return _peers[peerId]!;
  }

  /// Handles ICE candidates generated by the peer connection.
  /// If the remote description is not set, the candidate is stored locally.
  /// Otherwise, it's sent to the RMCS via MQTT.
  void onIceCandidate(RTCIceCandidate candidate, String peerId) {
    debugPrint('==================== START onIceCandidate ==================');
    if (candidate.candidate == null) {
      return;
    }

    final peer = _peers[peerId];
    if (peer == null) {
      return;
    }

    if (!peer.isRemoteDescription) {
      peer.robotIceCandidates.add(candidate);
    } else {
      mqttService.onPub(
        topic: '$_baseTopic/$peerId/$rmcsCandidateTopic',
        message: jsonEncode([candidate.toMap()]),
        qos: MqttQos.atMostOnce,
      );
    }

    debugPrint('==================== END onIceCandidate ==================');
  }

  /// Handles incoming MQTT messages from RMCS.
  /// It processes different topics like disconnect, offer, and candidate
  /// to manage WebRTC peer connections.
  Future<void> _handleMqttFromRMCS(String? topic, String payload) async {
    if (topic == null) {
      return;
    }

    final splitTopic = topic.split('/');
    String peerId = '';
    final tagRobotIndex = splitTopic.indexOf(robotControl);
    if (tagRobotIndex > 0) {
      peerId = splitTopic[tagRobotIndex + 1];
    }

    if (peerId.isEmpty) {
      return;
    }

    if (topic.contains(clientDisconnectTopic)) {
      debugPrint(
          ' ====== START _handleMqttFromRMCS $clientDisconnectTopic ======');
      await _disposePeerConnection(peerId);
      debugPrint(
          '======== END _handleMqttFromRMCS $clientDisconnectTopic ======');
      return;
    }

    // Create peer and register offer.
    if (topic.contains(offerTopic)) {
      debugPrint(
          ' ====== START _handleMqttFromRMCS $offerTopic $peerId ======');
      final peer = await setupPeerConnection(peerId);
      await peer.peerConnection
          .setRemoteDescription(RTCSessionDescription(payload, offerTopic));

      // Check signalingState
      final successState = peer.peerConnection.signalingState ==
              RTCSignalingState.RTCSignalingStateHaveRemoteOffer ||
          peer.peerConnection.signalingState ==
              RTCSignalingState.RTCSignalingStateHaveLocalOffer;

      if (successState) {
        final answer = await peer.peerConnection.createAnswer();
        await peer.peerConnection.setLocalDescription(answer);
        mqttService.onPub(
          topic: '$_baseTopic/$peerId/$answerTopic',
          message: answer.sdp!,
          qos: MqttQos.atMostOnce,
        );
      }
      debugPrint('======== END _handleMqttFromRMCS $offerTopic $peerId ======');
      return;
    }

    // Add ice candidate
    if (topic.contains(robotCandidateTopic)) {
      debugPrint(
          ' ====== START _handleMqttFromRMCS $robotCandidateTopic ======');

      if (!_peers.containsKey(peerId)) {
        return;
      }
      final candidates = jsonDecode(payload);
      final peer = _peers[peerId]!;
      peer.isRemoteDescription = true;

      for (final candidate in candidates) {
        final iceCandidate = RTCIceCandidate(
          candidate['candidate'],
          candidate['sdpMid'],
          candidate['sdpMLineIndex'],
        );
        peer.peerConnection.addCandidate(iceCandidate);
      }

      // Send robotIceCandidates to rmcs
      for (int i = 0;
          i < peer.robotIceCandidates.length;
          i += kMaxCandidateStorage) {
        final end = (i + kMaxCandidateStorage < peer.robotIceCandidates.length)
            ? i + kMaxCandidateStorage
            : peer.robotIceCandidates.length;
        final chunk = peer.robotIceCandidates.sublist(i, end);
        mqttService.onPub(
          topic: '$_baseTopic/$peerId/$rmcsCandidateTopic',
          message: jsonEncode(chunk.map((e) => e.toMap()).toList()),
          qos: MqttQos.atMostOnce,
        );
      }
      peer.robotIceCandidates.clear();
      debugPrint(
          '======== END _handleMqttFromRMCS $robotCandidateTopic ======');
    }
  }
}

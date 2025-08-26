// Project: Robot simulator
// Copyright (c) 2025 M2M craft Co., Ltd.

import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:flutter_webrtc/flutter_webrtc.dart';
import 'package:json_view/json_view.dart';
import 'package:robot_simulator/main.dart';
import 'package:robot_simulator/mqtt_service.dart';
import 'package:robot_simulator/tractor_camera_mock/tractor_camera_mock_state.dart';

enum Topic {
  permission('Permission', 'permission'),
  auxFunction('AUXInput', 'auxinput');

  const Topic(this.label, this.value);
  final String label;
  final String value;
}

class RTCPeerData {
  final RTCPeerConnection peerConnection;
  final RTCDataChannel channel;

  RTCPeerData({required this.peerConnection, required this.channel});
}

class TractorCameraScreen extends StatefulWidget {
  const TractorCameraScreen({super.key, required this.thingName});

  final String thingName;

  @override
  State<TractorCameraScreen> createState() => _TractorCameraScreenState();
}

class _TractorCameraScreenState extends State<TractorCameraScreen>
    with TractorCameraScreenState, TickerProviderStateMixin {
  late final TabController _tabController;
  final mqttService = locator<MqttService>();
  bool isConnection = false;

  @override
  void init() {
    _tabController = TabController(length: 3, vsync: this);
    super.init();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        backgroundColor: Colors.white,
        title: Text("TRACTOR CAMERA: ${widget.thingName}"),
        actions: [
          IconButton(
            onPressed: setTreeView,
            icon: Icon(
              isTreeView ? Icons.account_tree : Icons.code,
            ),
          ),
        ],
      ),
      body: _buildChild(),
    );
  }

  Widget _buildChild() {
    return Container(
      width: double.infinity,
      padding: const EdgeInsets.all(5),
      decoration: const BoxDecoration(
        // color: Colors.transparent,
        borderRadius: BorderRadius.all(Radius.circular(14)),
      ),
      child: Stack(
        children: [
          Column(
            children: [
              TabBar(
                controller: _tabController,
                onTap: setTab,
                tabs: [
                  const Tab(text: 'Camera'),
                  ...topics.map((data) {
                    final (t, isNotification, _) = data;
                    return _buildTab(
                      t.label,
                      getTopic(widget.thingName, t.value),
                      isNotification,
                    );
                  }),
                ],
              ),
              Expanded(
                child: TabBarView(
                  controller: _tabController,
                  children: [
                    RTCVideoView(localVideoRenderer, mirror: true),
                    ...topics.map((data) {
                      final (_, _, mqttData) = data;
                      if (mqttData.text.isEmpty) {
                        return const SizedBox();
                      }
                      if (isTreeView) {
                        return Container(
                          color: Colors.white,
                          child: JsonView(
                            json: json.decode(mqttData.text),
                          ),
                        );
                      }
                      return TextField(
                        controller: mqttData,
                        maxLines: null,
                        expands: true,
                        readOnly: true,
                        style: const TextStyle(fontSize: 14),
                        textAlignVertical: TextAlignVertical.top,
                        decoration: const InputDecoration(
                          filled: true,
                          fillColor: Colors.white,
                          border: OutlineInputBorder(
                            borderSide: BorderSide.none,
                          ),
                        ),
                      );
                    })
                  ],
                ),
              ),
            ],
          ),
          Positioned(
            top: 0,
            left: 0,
            right: 0,
            child: ValueListenableBuilder(
              valueListenable: mqttService.connectState,
              builder: (ctx, connected, child) {
                if (connected == ConnectState.connected) {
                  return const SizedBox();
                }
                return Container(
                  color: Colors.white,
                  child: const Text(
                    'MQTTの接続が失われました。再接続を試行しています...\nRMCSは接続を確立できません。',
                    textAlign: TextAlign.center,
                    style: TextStyle(
                      color: Colors.red,
                    ),
                  ),
                );
              },
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildTab(String title, String subTopic, bool isNotification) {
    return Tooltip(
      message: subTopic,
      child: Tab(
        child: Badge(
          label: isNotification
              ? const Icon(
                  Icons.notifications,
                  color: Colors.red,
                  size: 20,
                )
              : null,
          backgroundColor: Colors.transparent,
          offset: const Offset(5, -10),
          child: Text(title),
        ),
      ),
    );
  }
}

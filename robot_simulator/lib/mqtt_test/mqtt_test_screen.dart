// Project: Robot simulator
// Copyright (c) 2025 M2M craft Co., Ltd.

import 'dart:async';
import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:json_view/json_view.dart';
import 'package:robot_simulator/main.dart';
import 'package:robot_simulator/mqtt_test/mock.dart';
import 'package:robot_simulator/widgets/button.dart';
import 'package:robot_simulator/widgets/dropdown.dart';
import 'package:robot_simulator/widgets/toast.dart';

enum Topic {
  update('Update', 'update'),
  updateDocument('UpdateDocuments', 'update/documents'),
  permission('Permission', 'permission'),
  data('TC data', 'data'),
  serverCommand('Server Command', 'serverCommand'),
  auxFunction('AUXInput', 'auxinput');

  const Topic(this.label, this.value);
  final String label;
  final String value;
}

enum TopicSender {
  updateDocument('Update Document', 'update/documents'),
  tcLog('TC Log', 'data');

  const TopicSender(this.label, this.value);

  static final List<DropdownMenuEntry> entries = TopicSender.values
      .map((e) => DropdownMenuEntry(value: e.value, label: e.label))
      .toList();

  final String label;
  final String value;
}

class MqttTestScreen extends StatefulWidget {
  const MqttTestScreen({super.key, required this.thingName});

  final String thingName;

  @override
  State<MqttTestScreen> createState() => _MqttTestState();
}

class _MqttTestState extends State<MqttTestScreen>
    with TickerProviderStateMixin {
  static Timer? _timer;
  // Tab controller for the subscriber tabs
  late final TabController _tabController;
  // Text editing controller for the data send text field
  final TextEditingController _dataSendController = TextEditingController(
      text: mockUpdateDocument(DateTime.now().millisecondsSinceEpoch));
  // Flag to indicate if data is being sent at intervals
  bool flagSendInterval = false;
  // List of topics to subscribe to
  // dispose of the tab controller when the widget is disposed
  List<(Topic, bool, TextEditingController)> topics =
      Topic.values.map((e) => (e, false, TextEditingController())).toList();
  // Flag to indicate if the tree view is enabled
  bool isTreeView = false;

  String topicSender = TopicSender.updateDocument.value;

  @override
  void initState() {
    // Initialize the tab controller
    _tabController = TabController(length: topics.length, vsync: this);
    super.initState();
    // Subscribe to data received after the first frame
    WidgetsBinding.instance.addPostFrameCallback((_) async {
      subDataReceived();

      // Listen for incoming messages and update the corresponding text field
      mqttService.streamMessage.listen((data) {
        final (topic, msg) = data;
        if (topic == null ||
            !Topic.values.any((e) => topic.contains(e.value))) {
          return;
        }
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
      });
    });
  }

  @override
  void dispose() {
    _tabController.dispose();
    _timer?.cancel();
    mqttService.unSubscribe(
        topics.map((e) => getTopic(widget.thingName, e.$1.value)).toList());
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        backgroundColor: Colors.white,
        title: Text("MQTT TEST: ${widget.thingName}"),
      ),
      body: JsonConfig(
        data: JsonConfigData(
          gap: 200,
          style: const JsonStyleScheme(
            quotation: JsonQuotation.same(''),
            openAtStart: true,
            arrow: Icon(Icons.arrow_right),
            depth: 5,
          ),
          color: const JsonColorScheme(),
        ),
        child: Padding(
          padding: const EdgeInsets.all(10),
          child: _buildChild(),
        ),
      ),
    );
  }

  /// Builds the child widgets for the MQTT test screen.
  Widget _buildChild() {
    return Column(
      children: [
        const SizedBox(height: 5),
        Expanded(
          child: Row(
            children: [
              const SizedBox(height: 10),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  mainAxisAlignment: MainAxisAlignment.start,
                  children: [
                    SingleChildScrollView(
                      scrollDirection: Axis.horizontal,
                      child: Row(
                        children: [
                          SizedBox(
                            width: 120,
                            child: Dropdown(
                              onChanged: (value) => updateTopicSender(value),
                              dropdownMenuEntries: TopicSender.entries,
                              title: '',
                              value: topicSender,
                            ),
                          ),
                          const SizedBox(width: 10),
                          Button(
                            title: 'Send data',
                            width: 120,
                            onPressed: sendData,
                          ),
                          const SizedBox(width: 10),
                          Button(
                            title: 'Send data interval',
                            width: 180,
                            onPressed: sendDataInterval,
                            enable: !flagSendInterval,
                          ),
                          const SizedBox(width: 10),
                          Button(
                            title: 'Stop',
                            width: 100,
                            enable: flagSendInterval,
                            onPressed: stopInterval,
                          ),
                        ],
                      ),
                    ),
                    const SizedBox(height: 10),
                    Text(
                      'Publisher: ${widget.thingName}/${Topic.updateDocument.value}',
                      style: const TextStyle(
                          fontWeight: FontWeight.bold, fontSize: 12),
                    ),
                    Expanded(
                      child: TextField(
                        controller: _dataSendController,
                        maxLines: null,
                        expands: true,
                        style: const TextStyle(fontSize: 14),
                        decoration: const InputDecoration(
                          filled: true,
                          fillColor: Colors.white,
                          border: OutlineInputBorder(
                            borderSide: BorderSide.none,
                          ),
                        ),
                      ),
                    ),
                  ],
                ),
              ),
              const SizedBox(width: 10),
              Expanded(
                child: Column(
                  mainAxisSize: MainAxisSize.min,
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Row(
                      children: [
                        const Text(
                          'Subscriber',
                          style: TextStyle(
                            fontWeight: FontWeight.bold,
                          ),
                        ),
                        Expanded(
                          child: TabBar(
                            controller: _tabController,
                            indicatorSize: TabBarIndicatorSize.label,
                            padding: EdgeInsets.zero,
                            isScrollable: true,
                            tabAlignment: TabAlignment.start,
                            onTap: (index) {
                              setState(() {
                                final (t, isNotification, mqttData) =
                                    topics[index];
                                topics[index] = (t, false, mqttData);
                              });
                            },
                            tabs: topics.map((data) {
                              final (t, isNotification, _) = data;
                              return _buildTab(
                                t.label,
                                getTopic(widget.thingName, t.value),
                                isNotification,
                              );
                            }).toList(),
                          ),
                        ),
                      ],
                    ),
                    const Text(
                      ' ',
                      style: TextStyle(
                        fontWeight: FontWeight.bold,
                        color: Colors.transparent,
                        fontSize: 12
                      ),
                    ),
                    Expanded(
                      child: Stack(
                        children: [
                          TabBarView(
                            physics: const NeverScrollableScrollPhysics(),
                            controller: _tabController,
                            children: topics.map((data) {
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
                            }).toList(),
                          ),
                          Positioned(
                            right: 0,
                            child: Row(
                              children: [
                                IconButton(
                                  onPressed: () {
                                    setState(() {
                                      isTreeView = false;
                                    });
                                  },
                                  icon: Icon(
                                    Icons.code,
                                    size: 20,
                                    color:
                                        !isTreeView ? Colors.blueAccent : null,
                                  ),
                                ),
                                const SizedBox(width: 10),
                                IconButton(
                                  onPressed: () {
                                    setState(() {
                                      isTreeView = true;
                                    });
                                  },
                                  icon: Icon(
                                    Icons.account_tree,
                                    size: 20,
                                    color:
                                        isTreeView ? Colors.blueAccent : null,
                                  ),
                                ),
                              ],
                            ),
                          )
                        ],
                      ),
                    ),
                  ],
                ),
              ),
            ],
          ),
        ),
      ],
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

  void updateTopicSender(String topic) {
    setState(() {
      topicSender = topic;
    });
    if (topic == TopicSender.updateDocument.value) {
      _dataSendController.text =
          mockUpdateDocument(DateTime.now().millisecondsSinceEpoch);
    } else {
      _dataSendController.text = mockTCLogData();
    }
  }

  /// Sends data over MQTT.
  Future<void> sendData() async {
    if (topicSender == TopicSender.tcLog.value) {
      mqttService.onPub(
        topic: getTopic(widget.thingName, TopicSender.tcLog.value),
        message: _dataSendController.text,
      );
      showToast('Sent', context);
      return;
    }
    try {
      // Get data and update datetime
      final dataCln = jsonDecode(_dataSendController.text);

      // Update datetime of realtime

      dataCln['mTmGw'] = DateTime.now().millisecondsSinceEpoch;
      dataCln['instruction']?['mTmMsc'] = DateTime.now().millisecondsSinceEpoch;
      dataCln['instruction']?['GPS position']?['mTmMsc'] =
          DateTime.now().millisecondsSinceEpoch;

      // Convert to String message
      final message = jsonEncode(dataCln);

      // Pretty Json to Show on Screen
      const encoder = JsonEncoder.withIndent('  ');
      final object = const JsonDecoder().convert(message);
      final prettyString = encoder.convert(object);

      // Show on screen
      _dataSendController.text = prettyString;

      // Public message
      mqttService.onPub(
        topic: '${widget.thingName}/${TopicSender.updateDocument.value}',
        message: message,
      );
      showToast('Sent', context);
    } catch (e) {
      debugPrint(e.toString());
      showToast(
        'JSON data is not in the correct format. ${e.toString()}',
        context,
        isError: true,
      );
    }
  }

  /// Sends data at regular intervals.
  Future<void> sendDataInterval() async {
    await sendData();
    // Start a timer to send data every 5 seconds.
    _timer = Timer.periodic(const Duration(seconds: 5), (timer) async {
      await sendData();
    });
    setState(() {
      // Set the flag to indicate that data is being sent at intervals.
      flagSendInterval = true;
    });
  }

  /// Stops sending data at regular intervals.
  Future<void> stopInterval() async {
    // Cancel the timer.
    _timer?.cancel();
    setState(() {
      flagSendInterval = false;
    });
  }

  /// Subscribes to data topics and handles received messages.
  Future<void> subDataReceived() async {
    // Subscribe to the specified topics.
    mqttService.onSubs(
        topics: Topic.values
            .map((e) => getTopic(widget.thingName, e.value))
            .toList());
    showToast('Sent', context, isError: true);
    // Show a toast message indicating that the robot has subscribed.
    showToast(
      'Subscribe robot has thingname: ${widget.thingName}',
      context,
    );
  }

  String getTopic(String thingName, String subTopic) {
    return '$thingName/$subTopic';
  }
}

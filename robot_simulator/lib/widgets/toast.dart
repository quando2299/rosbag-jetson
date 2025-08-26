// Project: robot_simulator
// Copyright (c) 2024 M2M craft Co., Ltd.

import 'package:flutter/material.dart';
import 'package:flutter_styled_toast/flutter_styled_toast.dart'
    as styledShowToast;

Future<void> showToast(
  String msg,
  BuildContext context, {
  bool isError = false,
}) async {
  styledShowToast.dismissAllToast();
  styledShowToast.showToast(
    msg,
    context: context,
    textStyle: const TextStyle(fontSize: 14, color: Colors.white),
    textAlign: TextAlign.start,
    backgroundColor:
        isError ? const Color(0xFFD32F2F) : const Color(0xFF1A932E),
    position: const styledShowToast.StyledToastPosition(
      align: Alignment.bottomRight,
    ),
    duration: const Duration(milliseconds: 1500),
    animDuration: const Duration(milliseconds: 200),
    axis: Axis.horizontal,
    borderRadius: BorderRadius.circular(3),
    toastHorizontalMargin: 0,
  );
}

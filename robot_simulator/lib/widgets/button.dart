// Project: camera_simulator
// Copyright (c) 2024 M2M craft Co., Ltd.

import 'package:flutter/material.dart';

Widget Button({
  required String title,
  bool enable = true,
  VoidCallback? onPressed,
  double width = 300,
  double height = 40,
  bool isLoading = false,
}) {
  return SizedBox(
    child: ElevatedButton.icon(
      style: ElevatedButton.styleFrom(
        backgroundColor: const Color(0xFF004F98),
        shape: const RoundedRectangleBorder(
          borderRadius: BorderRadius.all(Radius.circular(5)),
        ),
        minimumSize: Size(width, height),
      ),
      onPressed: enable ? onPressed : null,
      icon: isLoading
          ? Container(
              width: 24,
              height: 24,
              padding: const EdgeInsets.all(2.0),
              child: const CircularProgressIndicator(
                color: Colors.white,
                strokeWidth: 3,
              ),
            )
          : null,
      label: Text(
        title,
        textAlign: TextAlign.center,
        style: TextStyle(
          color: enable ? Colors.white : Colors.black.withOpacity(0.4),
        ),
      ),
    ),
  );
}

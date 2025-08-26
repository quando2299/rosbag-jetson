// Project: robot_simulator
// Copyright (c) 2024 M2M craft Co., Ltd.

import 'package:flutter/material.dart';

Widget Textbox({
  TextEditingController? controller,
  String? hintText,
  bool enabled = true,
  String? errorText,
  TapRegionCallback? onTapOutside,
  GestureTapCallback? onTap,
}) {
  return Container(
    height: 40,
    alignment: Alignment.centerLeft,
    decoration: BoxDecoration(
      border: Border.all(color: Colors.black, width: 1),
      color: Colors.white,
      borderRadius: BorderRadius.circular(5),
    ),
    child: TextField(
      enabled: enabled,
      onTapOutside: onTapOutside,
      onTap: onTap,
      decoration: InputDecoration(
        hintText: hintText,
        fillColor: Colors.white,
        focusColor: Colors.white,
        isDense: true,
        contentPadding: const EdgeInsets.symmetric(horizontal: 10),
        hintStyle: const TextStyle(color: Colors.grey),
        border: const OutlineInputBorder(
          borderSide: BorderSide.none,
        ),
        errorText: errorText,
      ),
      controller: controller,
    ),
  );
}

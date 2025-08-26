// Project: RMCS
// Copyright (c) 2024 M2M craft Co., Ltd.


import 'package:flutter/material.dart'
    hide DropdownButton, DropdownButtonHideUnderline, DropdownMenuItem;
import 'package:robot_simulator/widgets/custom/dropdown.dart';

class Dropdown<T> extends StatelessWidget {
  const Dropdown({
    required this.onChanged,
    required this.dropdownMenuEntries,
    required this.title,
    super.key,
    this.height = 40,
    this.value,
    this.isDense = true,
    this.isShowIcon = true,
    this.isBorder = true,
    this.itemHeight = 40,
    this.isDisable = false,
    this.required = false,
    this.bgColor,
  });

  final String title;
  final dynamic value;
  final List<DropdownMenuEntry<T>> dropdownMenuEntries;
  final double? height;
  final double? itemHeight;
  final ValueChanged<T> onChanged;
  final bool isDense;
  final bool isShowIcon;
  final bool isBorder;
  final bool isDisable;
  final bool? required;
  final Color? bgColor;

  @override
  Widget build(BuildContext context) {
    return IgnorePointer(
      ignoring: isDisable,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Visibility(
            visible: title.isNotEmpty,
            child: RichText(
              text: TextSpan(
                children: [
                  TextSpan(
                    text: title,
                    style: const TextStyle(fontSize: 14),
                  ),
                  TextSpan(
                    text: required ?? false ? ' *' : '',
                    style: const TextStyle(color: Colors.red),
                  ),
                ],
              ),
            ),
          ),
          Container(
            height: 40,
            decoration: BoxDecoration(
              borderRadius: BorderRadius.circular(4),
              border: isBorder
                  ? Border.all(
                      width: 1,
                      color: const Color(0xFF000000).withOpacity(0.4))
                  : null,
              color: const Color(0xFFFFFFFF),
            ),
            child: DropdownButtonHideUnderline(
              child: DropdownButton<T>(
                menuMaxHeight: 500,
                alignment: AlignmentDirectional.bottomCenter,
                itemHeight: 40,
                icon: isShowIcon ? null : const SizedBox(),
                isExpanded: true,
                isDense: isDense,
                value: value,
                style: const TextStyle(
                  overflow: TextOverflow.ellipsis,
                  height: 1.5,
                ),
                padding: isDense
                    ? const EdgeInsets.symmetric(vertical: 0, horizontal: 10)
                    : EdgeInsets.zero,
                items: dropdownMenuEntries.map((e) {
                  return DropdownMenuItem(
                    value: e.value,
                    enabled: e.enabled,
                    child: e.labelWidget != null
                        ? Opacity(
                            opacity: e.enabled ? 1 : 0.5,
                            child: e.labelWidget,
                          )
                        : Container(
                            alignment: Alignment.centerLeft,
                            height: height,
                            child: Opacity(
                              opacity: e.enabled ? 1 : 0.5,
                              child: Text(
                                overflow: TextOverflow.ellipsis,
                                e.label,
                                style: Theme.of(context).textTheme.labelMedium,
                              ),
                            ),
                          ),
                  );
                }).toList(),
                onChanged: (value) {
                  if (value == null) {
                    return;
                  }
                  onChanged(value);
                },
              ),
            ),
          ),
        ],
      ),
    );
  }
}

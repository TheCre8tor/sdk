// Copyright (c) 2022, the Dart project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

import 'package:analysis_server/src/lsp/lsp_analysis_server.dart';
import 'package:analysis_server/src/services/correction/change_workspace.dart';
import 'package:analysis_server/src/services/correction/util.dart';
import 'package:analyzer/dart/analysis/results.dart';
import 'package:analyzer/dart/analysis/session.dart';
import 'package:analyzer/dart/ast/ast.dart';
import 'package:analyzer/src/dart/analysis/session_helper.dart';
import 'package:analyzer/src/utilities/extensions/ast.dart';
import 'package:analyzer_plugin/utilities/change_builder/change_workspace.dart';

/// The context in which a refactoring was requested.
class RefactoringContext {
  final LspAnalysisServer server;

  /// The result of resolving the library in which a refactoring was requested.
  final ResolvedLibraryResult resolvedLibraryResult;

  /// The result of resolving the compilation unit in which a refactoring was
  /// requested. This will always be one of the `units` from the
  /// [resolvedLibraryResult].
  final ResolvedUnitResult resolvedUnitResult;

  /// The offset to the beginning of the selection range.
  final int selectionOffset;

  /// The number of selected characters.
  final int selectionLength;

  /// Utilities available to be used in the process of computing the edits.
  late final CorrectionUtils utils = CorrectionUtils(resolvedUnitResult);

  /// The node that was selected, or `null` if the selection is not valid.
  late final AstNode? selectedNode = resolvedUnitResult.unit
      .nodeCovering(offset: selectionOffset, length: selectionLength);

  /// The helper used to efficiently access resolved units.
  late final AnalysisSessionHelper sessionHelper =
      AnalysisSessionHelper(session);

  /// The change workspace associated with this refactoring.
  late final ChangeWorkspace workspace = DartChangeWorkspace([session]);

  /// Initialize a newly created refactoring context.
  RefactoringContext({
    required this.server,
    required this.resolvedLibraryResult,
    required this.resolvedUnitResult,
    required this.selectionOffset,
    required this.selectionLength,
  });

  /// Return the analysis session in which additional resolution can occur.
  AnalysisSession get session => resolvedUnitResult.session;
}

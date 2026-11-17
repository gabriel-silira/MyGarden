import 'package:flutter/foundation.dart';
import 'package:path/path.dart' as path;
import 'package:path_provider/path_provider.dart';
import 'package:sembast/sembast_io.dart';
import 'package:sembast_web/sembast_web.dart';

import 'sembast_history_store.dart';

SembastHistoryStore createLocalHistoryStore({
  String databaseName = 'mygarden-history-v1.db',
}) => SembastHistoryStore(
  openDatabase: () async {
    if (kIsWeb) {
      return databaseFactoryWeb.openDatabase(databaseName, version: 1);
    }
    final directory = await getApplicationSupportDirectory();
    await directory.create(recursive: true);
    return databaseFactoryIo.openDatabase(
      path.join(directory.path, databaseName),
      version: 1,
    );
  },
);

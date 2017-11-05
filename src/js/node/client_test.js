'use strict';
var hypertable = require('hypertable');
var util = require('util');
var async = require('async');

/**
 * This callback type is called `requestCallback` and is displayed as a global
 * symbol.
 * @callback requestCallback
 * @param {Object} exception Exception object
 * @param {string} responseMessage Response message
 */

/**
 * Hypertable client
 * @type {Object}
 */
var client = new hypertable.ThriftClient("localhost", 15867);

/**
 * Holds 'test' namespace ID
 * @type {Int64}
 */
var testNamespace;

function validateScanProfileData(label, scanner, profile_data) {
  if (profile_data.servers.length != 1) {
    console.log("[%s] Expected profile data to have non-empty servers field",
                label);
    process.exit(1);
  }
  if (profile_data.servers[0] != "rs1") {
    console.log("[%s] Expected profile data servers to contain rs1 but got %s",
                label, profile_data.servers[0]);
    process.exit(1);
  }
  if (scanner != 0 && profile_data.id.value != scanner.value) {
    console.log("[%s] Expected profile data to have id %d but got %d",
                label, scanner, profile_data.id);
    process.exit(1);
  }
  if (profile_data.bytes_scanned == 0 || profile_data.bytes_returned == 0) {
    console.log("[%s] Expected profile data to have non-zero bytes scanned and"+
                " returned but got bytes_scanned=%d and and bytes_returned=%d",
                label, profile_data.bytes_scanned, profile_data.bytes_returned);
    process.exit(1);
  }
  if (profile_data.cells_scanned == 0 || profile_data.cells_returned == 0) {
    console.log("[%s] Expected profile data to have non-zero cells scanned and"+
                " returned but got cells_scanned=%d and and cells_returned=%d",
                label, profile_data.cells_scanned, profile_data.cells_returned);
    process.exit(1);
  }
  if (profile_data.subscanners != 1) {
    console.log("[%s] Expected profile data to have 1 subscanner but got %d",
                label, profile_data.subscanners);
    process.exit(1);
  }
} 


/**
 * Opens the 'test' namespace.  Assigns the returned namespace ID to the
 * variable {@link testNamespace}.  
 * @param {requestCallback} callback - Callback object used for chaining
 */
var openNamespaceTest = function (callback) {
  console.log('[openNamespaceTest]');
  client.namespace_open("test", function (error, result) {
    if (error)
      console.log(util.format('%s: %s', error.name, error.message));
    else
      testNamespace = result;
    callback(null, 'openNamespaceTest');
  });
}

/**
 * Opens the 'bad' namespace.  Verifies that the namespace_open call gets called
 * back with an Exception and displays the exception class name and exception
 * message to the console.
 * @param {requestCallback} callback - Callback object used for chaining
 */
var badNamespaceTest = function (callback) {
  console.log('[badNamespaceTest]');
  client.namespace_open("bad", function (error, result) {
      if (error)
        console.log(util.format('%s: %s', error.name, error.message));
      else
        assert(false, "Namespace 'bad' should not exist!");
      callback(null, 'badNamespaceTest');
    });
}

/**
 * Issues the HQL command <code>SHOW TABLES</code> and displays results to the
 * console.
 * @param {requestCallback} callback - Callback object used for chaining
 */
var showTablesTest = function(callback) {
  console.log('[showTablesTest]');
  var namespace;
  async.waterfall([
    function openNamespace (callback) {
      client.namespace_open("test", callback);
    },
    function getListing (ns, callback) {
      namespace = ns;
      client.hql_query(ns, "SHOW TABLES", callback);
    },
    function processGetListingResponse (response, callback) {
      for (var i = 0; i < response.results.length; i++) {
        console.log(util.format('%s', response.results[i]));
      }
      client.namespace_close(namespace, callback);
    },
    function closeNamespace (responsee, callback) {
      callback();
    }
    ],
    function(error) { callback(error, 'showTablesTest'); });
}

/**
 * Issues the HQL command <code>SELECT * FROM thrift_test LIMIT 10</code> and
 * displays the result to the console.
 * @param {requestCallback} callback - Callback object used for chaining
 */
var selectTestTable = function(callback) {
  console.log('[selectTestTable]');
  async.waterfall([
    function getListing (callback) {
      client.hql_query(testNamespace, "SELECT * FROM thrift_test LIMIT 10",
                       callback);
    },
    function processGetListingResponse (response, callback) {
      for (var i = 0; i < response.cells.length; i++) {
        console.log(response.cells[i].toString());
      }
      validateScanProfileData("HqlResult", 0, response.scan_profile_data);
      callback(null);
    }
  ],
  function(error) { callback(error, 'selectTestTable'); });
}

/**
 * Inserts a test Cell into the table <code>thrift_test</code> using a mutator.
 * @param {requestCallback} callback - Callback object used for chaining
 */
var mutatorTest = function (callback) {
  console.log('[mutatorTest]');
  var theMutator;
  async.waterfall([
    function mutatorOpen(callback) {
      client.mutator_open(testNamespace, "thrift_test", 0, 0, callback);
    },
    function setCell(mutator, callback) {
      theMutator = mutator;
      var key = new hypertable.Key({row: "js-k1", column_family: "col"});
      client.mutator_set_cell(theMutator,
                              new hypertable.Cell({ key: key, value: "js-v1"}),
                              callback);
    },
    function closeMutator(mutator, callback) {
      client.mutator_close(theMutator, callback);
    }
  ],
  function(error) { callback(error, 'mutatorTest'); });
}

/**
 * Inserts a test Cell into the table <code>thrift_test</code> using a shared
 * mutator.
 * @param {requestCallback} callback - Callback object used for chaining
 */
var sharedMutatorTest = function (callback) {
  console.log('[sharedMutatorTest]');
  var mutateSpec = new hypertable.MutateSpec({appname: 'test_js',
                                              flush_interval: 1000,
                                              flags: 0});
  async.waterfall([
    function setCell1(callback) {
      var key = new hypertable.Key({row: "js-put-k1", column_family: "col"});
      client.shared_mutator_set_cell(testNamespace, "thrift_test", mutateSpec,
                                     new hypertable.Cell({key: key, value: "js-put-v1"}),
                                     callback);
    },
    function refresh(mutator, callback) {
      client.shared_mutator_refresh(testNamespace, "thrift_test", mutateSpec,
                                    callback);
    },
    function setCell2(result, callback) {
      var key = new hypertable.Key({row: "js-put-k2", column_family: "col"});
      client.shared_mutator_set_cell(testNamespace, "thrift_test", mutateSpec,
                                     new hypertable.Cell({key: key, value: "js-put-v2"}),
                                     callback);
    },
    function processResult(result, callback) {
      setTimeout(function() { callback(null); }, 2000);
    }
  ],
  function(error) { callback(error, 'sharedMutatorTest'); });
}

/**
 * Creates a scanner and Scans over the first 10 rows of the the table
 * <code>thrift_test</code> displaying the results to the console.
 * @param {requestCallback} callback - Callback object used for chaining
 */
var scannerTest = function (callback) {
  console.log('[scannerTest]');
  var scanner;
  async.waterfall([
    function createScanner(callback) {
      var scanSpec = new hypertable.ScanSpec({row_limit: 10});
      client.scanner_open(testNamespace, "thrift_test", scanSpec, callback);
    },
    function getCells(result, callback) {
      scanner = result;
      client.scanner_get_cells(scanner, callback);
    },
    function processCells(result, callback) {
      for (var i = 0; i < result.length; i++) {
        console.log(result[i].toString());
      }
      client.scanner_get_profile_data(scanner, callback);
    },
    function validateProfileData(result, callback) {
      console.log(result);
      validateScanProfileData("HqlResult", scanner, result);
      client.scanner_close(scanner, callback);
    },
    function handleCloseMutatorResult(result, callback) {
      callback(null);
    }
  ],
  function(error) { callback(error, 'scannerTest'); });

}

var asyncMutatorPipeline = function(future, keyPrefix, callback) {
  var mutator;
  async.waterfall([
    function createAsyncMutator1(callback) {
      client.async_mutator_open(testNamespace, "thrift_test", future, 0, callback);
    },
    function asyncMutate1(result, callback) {
      mutator = result;
      var key = new hypertable.Key({row: keyPrefix, column_family: "col"});
      client.async_mutator_set_cell(mutator,
                                    new hypertable.Cell({key: key, value: keyPrefix+"-async"}),
                                    callback);
    },
    function asyncMutatorFlush1(result, callback) {
      client.async_mutator_flush(mutator, callback);
    },
    function handleAsyncMutatorFlushResult1(result, callback) {
      callback();
    }
  ],
  function (error, result) {
    callback(error, mutator);
  });
}


var asyncTest = function (callback) {
  console.log('[asyncTest]');
  var future;
  var asyncMutator = [];
  var asyncScanner = [];
  async.waterfall([
    function (callback) {
      client.future_open(0, callback);
    },
    function (result, callback) {
      future = result;
      callback();
    },
    function runAsyncMutators(callback) {
      async.parallel([
        function (callback) {
          asyncMutatorPipeline(future, "js-k1", callback);
        },
        function (callback) {
          asyncMutatorPipeline(future, "js-k2", callback);
        }
      ],
      function(error, results) {
        asyncMutator = results;
        callback(error);
      });
    },
    function fetchResults (callback) {
      var numResults=0;
      var futureResult;
      async.doWhilst(
        function (callback) {
          async.waterfall([
            function fetchFutureResult(callback) {
              client.future_get_result(future, 10000, callback);
            },
            function processFutureResult(result, callback) {
              futureResult = result;
              if (!futureResult.is_empty) {
                numResults++;
                if (futureResult.is_error || futureResult.is_scan)
                  callback(new Error('Unexpected result'));
                else
                  callback(null);
              }
              else
                callback(null);
            }
          ],
          function(error) { callback(error); });
        },
        function () { return !futureResult.is_empty; },
        function (err) {
          if (numResults != 2)
            callback(new Error('Expected 2 results, got ' + numResults));
          else
            callback(null);
        }                     
      );
    },
    function validateFuture (callback) {
      async.parallel([
        function (callback) {
          client.future_is_cancelled(future, callback);
        },
        function (callback) {
          client.future_is_full(future, callback);
        },
        function (callback) {
          client.future_is_empty(future, callback);
        },
        function (callback) {
          client.future_has_outstanding(future, callback);
        },
      ],
      function (error, results) {
        if (error)
          callback(error);
        else if (results[0])
          callback(new Error('Future was cancelled'));
        else if (results[1])
          callback(new Error('Future is full'));
        else if (!results[2])
          callback(new Error('Future is not empty'));
        else if (results[3])
          callback(new Error('Future has outstanding'));
        else
          callback();
      });
    },
    function closeMutators (callback) {
      async.parallel([
        function (callback) {
          client.async_mutator_close(asyncMutator[0], callback);
        },
        function (callback) {
          client.async_mutator_close(asyncMutator[1], callback);
        }
      ],
      function(error) { callback(error); });
    },
    function createScanners (callback) {
      async.parallel([
        function (callback) {
          client.async_scanner_open(testNamespace, "FruitColor", future,
                                    new hypertable.ScanSpec({versions: 1}),
                                    callback);

        },
        function (callback) {
          client.async_scanner_open(testNamespace, "FruitLocation", future,
                                    new hypertable.ScanSpec({versions: 1}),
                                    callback);

        },
        function (callback) {
          client.async_scanner_open(testNamespace, "FruitEnergy", future,
                                    new hypertable.ScanSpec({versions: 1}),
                                    callback);

        }
      ],
      function(error, results) {
        asyncScanner = results;
        callback(error);
      });
    },
    function fetchScanResults (callback) {
      var num_cells = 0;
      async.doWhilst(
        function (callback) {
          async.waterfall([
            function (callback) {
              client.future_get_result(future, 10000, callback);
            },
            function (result, callback) {
              if (result.is_empty || result.is_error || !result.is_scan)
                callback(new Error('Unexpected result'));
              else {
                for (var i = 0; i < result.cells.length; i++)
                  console.log(result.cells[i].toString());
                num_cells += result.cells.length;
                callback();
              }
            }
          ],
          function(error) { callback(error); });
        },
        function () { return num_cells < 6; },
        function (err) {
          callback(err);
        }
      );
    },
    function cancelFuture (callback) {
      async.waterfall([
        function (callback) {
          client.future_cancel(future, callback);
        },
        function (result, callback) {
          client.future_is_cancelled(future, callback);
        },
        function (result, callback) {
          if (!result)
            callback(new Error('Expected future to be cancelled'));
          else
            callback();
        }
      ],
      function(error) { callback(error); });
    },
    function asyncScannerRegex (callback) {
      var scanner;
      async.waterfall([
        function (callback) {
          var scanSpec =
            new hypertable.ScanSpec({versions: 1, columns: ["col"],
                                     row_regexp: "k", value_regexp: "v[24]"});
          client.scanner_open(testNamespace, "thrift_test", scanSpec, callback);
        },
        function (result, callback) {
          scanner = result;
          var emptyResults = false;
          async.doWhilst(
            function (callback) {
              async.waterfall([
                function (callback) {
                  client.scanner_get_cells(scanner, callback);
                },
                function (cells, callback) {
                  if (cells.length == 0)
                    emptyResults=true;
                  callback();
                }
              ],
              function(error) { callback(error); });
            },
            function () { return !emptyResults;; },
            function (err) {
              callback(err);
            }
          );
        },
        function (callback) {
          client.async_scanner_close(scanner, callback);
        }
      ],
      function(error) { callback(error); });
    },
    function validateProfileData (callback) {
      async.parallel([
        function (callback) {
          client.async_scanner_get_profile_data(asyncScanner[0], callback);
        },
        function (callback) {
          client.async_scanner_get_profile_data(asyncScanner[1], callback);
        },
        function (callback) {
          client.async_scanner_get_profile_data(asyncScanner[2], callback);
        }
      ],
      function(error, results) {
        validateScanProfileData("Async Scanner 0", asyncScanner[0], results[0]);
        validateScanProfileData("Async Scanner 1", asyncScanner[1], results[1]);
        validateScanProfileData("Async Scanner 2", asyncScanner[2], results[2]);
        callback(error);
      });
    },
    function cleanup (callback) {
      async.series([
        function (callback) {
          client.future_close(future, callback);
        },
        function (callback) {
          async.parallel([
            function (callback) {
              client.async_scanner_close(asyncScanner[0], callback);
            },
            function (callback) {
              client.async_scanner_close(asyncScanner[1], callback);
            },
            function (callback) {
              client.async_scanner_close(asyncScanner[2], callback);
            }
          ],
          function(error) { callback(error); });
        }
      ],
      function(error) { callback(error); });
    }
  ],
  function(error) { callback(error, 'asyncTest'); });
}
    

/**
 * Closes the 'test' namespace.  Namespace ID is held in {@link testNamespace}.  
 * @param {requestCallback} callback - Callback object used for chaining
 */
var closeNamespaceTest = function (callback) {
  console.log('[closeNamespaceTest]');
  client.namespace_close(testNamespace, function (error, result) {
    testNamespace = 0;
    callback(error, 'closeNamespaceTest');
  });
}

console.log("HQL examples");

async.series([
  openNamespaceTest,
  badNamespaceTest,
  showTablesTest,
  selectTestTable,
  mutatorTest,
  sharedMutatorTest,
  scannerTest,
  asyncTest,
  closeNamespaceTest
  ],
  function(error, results) {
    if (error)
      console.log(util.format('%s: %s', error.name, error.message));
    client.closeConnection();
  }
);

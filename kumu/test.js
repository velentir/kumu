const assert = require('assert');
const fs = require('fs');
const jsdom = require('jsdom');
const { JSDOM } = jsdom;

/**
 * Add assertion method used by kumu tests so that it
 * is available to test functions.
 */
function ASSERT_EQ(expected, actual) {
  assert(expected === actual, 'ASSERT_EQ failed: \n  Actual: "' + actual + '"\n  Expected: "' + expected + '"');
}

/**
 * Add assertion method used by kumu tests so that it
 * is available to test functions.
 */
function ASSERT_NULL(actual) {
  assert(actual == null, 'ASSERT_NULL failed: \n  Actual: "' + actual + '"');
}

/**
 * Pulls tests of the given type from the XML and passes
 * each to the callback.
 */
function execTests(document, tag, callback) {
  const tests = document.querySelectorAll(tag);
  for (var i = 0; i < tests.length; i++) {
    let name = tests[i].querySelector("name").textContent;
    let code = tests[i].querySelector("code").textContent;
    callback(name, code);
  }
}

/**
 * Main script function.
 */
function main() {

  if (process.argv.length != 3) {
    console.error('Expected one argument - the file path of the tests.')
    process.exit(1);
  }

  let tests_filepath = process.argv[2];

  const data = fs.readFileSync(tests_filepath, 'utf8');

  const dom = new JSDOM(data);
  const document = dom.window.document;

  let passed = 0;
  let failed = 0;

  execTests(document, 'test', (name, code) => {
    try {
      eval(code);
      passed += 1;
    } catch (e) {
      console.log('[TEST ' + name + '] FAILED with error: ' + e);
      failed += 1;
    }
  });

  execTests(document, 'syntax_failure', (name, code) => {
    try {
      eval(code);
    } catch (e) {
      if (e instanceof SyntaxError) {
        passed += 1;
        return;
      }
    }
    console.log('[SYNTAX TEST ' + name + '] FAILED -- no syntax error detected.');
    failed += 1;
  });

  execTests(document, 'runtime_failure', (name, code) => {
    try {
      eval(code);
      console.log('[RUNTIME TEST ' + name + '] FAILED -- no runtime error detected.');
      failed += 1;
    } catch (e) {
      if (e instanceof SyntaxError) {
        throw(e);
      }
      passed += 1;
    }
  });

  console.log("");
  console.log("------------------------");
  console.log("---- TESTS COMPLETE ----");
  console.log("------------------------");
  console.log("[  PASSED  ] %d test", passed);
  console.log("[  FAILED  ] %d test", failed);
}

main();

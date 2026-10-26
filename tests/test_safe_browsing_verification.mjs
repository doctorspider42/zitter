import test from 'node:test';
import assert from 'node:assert/strict';
import {assessRecord} from '../tools/verify_safe_browsing.mjs';

function evidence({injected = false, status = 'HTTP/1.1 200', warning = true} = {}) {
  return {netlog_complete: true, warning_assertion: {passed: warning, injected},
    back_to_safety: {result: {value: {url: 'http://127.0.0.1:1234/fixture'}}},
    network: {requests: [{hops: [{host: 'safebrowsing.googleapis.com',
      endpoint: 'https://safebrowsing.googleapis.com/v5/hashLists:batchGet',
      response_status_lines: [status]}]}]}};
}
test('injected warning cannot certify real-list protection', () => {
  const record = evidence({injected: true});
  assert.equal(assessRecord(0, record, false).passed, true);
  assert.equal(assessRecord(0, record, true).passed, false);
});
test('HTTP 400 or no list update fails the live release gate', () => {
  assert.equal(assessRecord(0, evidence({status: 'HTTP/1.1 400'}), true).passed, false);
  const record = evidence(); record.network.requests = [];
  assert.equal(assessRecord(0, record, true).passed, false);
});
test('successful list download without a warning fails the live gate', () => {
  assert.equal(assessRecord(0, evidence({warning: false}), true).passed, false);
});
test('complete real-list and warning evidence is required', () => {
  assert.equal(assessRecord(0, evidence(), true).passed, true);
  assert.equal(assessRecord(1, evidence(), true).passed, false);
  const record = evidence(); record.netlog_complete = false;
  assert.equal(assessRecord(0, record, true).passed, false);
  assert.equal(assessRecord(0, undefined, true).passed, false);
});
test('safe-return failure cannot pass even with warning and valid lists', () => {
  const record = evidence(); record.back_to_safety.result.value.url = 'https://example.com/';
  assert.equal(assessRecord(0, record, true).passed, false);
});

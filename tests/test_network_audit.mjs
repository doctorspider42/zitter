import test from 'node:test';
import assert from 'node:assert/strict';
import {endpoint, summarizeNetlog} from '../tools/audit_background_network.mjs';

const types = {URL_REQUEST_START_JOB: 81, HTTP_TRANSACTION_READ_RESPONSE_HEADERS: 94,
  URL_REQUEST_REDIRECTED: 95, URL_REQUEST_END: 96, HOST_RESOLVER_MANAGER_REQUEST: 12,
  TCP_CONNECT_ATTEMPT: 16, SSL_CONNECT: 17};
const phases = {PHASE_BEGIN: 1, PHASE_END: 2, PHASE_NONE: 0};
const event = (name, id, params = {}, phase = 0, sourceType = 1) => ({
  type: types[name], phase, source: {id, type: sourceType}, time: '123', params
});
const log = events => ({constants: {logEventTypes: types, logEventPhase: phases}, events});

test('shareable endpoints omit credentials, queries and fragments', () => {
  assert.deepEqual(endpoint('https://name:secret@api.example/v1/list?key=secret#private'), {
    host: 'api.example', endpoint: 'https://api.example/v1/list'
  });
  assert.equal(endpoint('chrome://settings'), null);
  assert.equal(endpoint('invalid'), null);
});

test('a URL request is evidence of an attempt, not proof of server success', () => {
  const result = summarizeNetlog(log([
    event('URL_REQUEST_START_JOB', 4, {url: 'https://api.example/v1/query'}, 1),
    event('URL_REQUEST_END', 4, {net_error: -105}, 2)
  ]));
  assert.equal(result.hosts['api.example'].requests, 1);
  assert.deepEqual(result.hosts['api.example'].response_status_lines, []);
  assert.deepEqual(result.hosts['api.example'].net_errors, [-105]);
});

test('HTTP 400 remains a server rejection rather than a successful service operation', () => {
  const result = summarizeNetlog(log([
    event('URL_REQUEST_START_JOB', 5, {url: 'https://api.example/v1/query', method: 'GET'}, 1),
    event('HTTP_TRANSACTION_READ_RESPONSE_HEADERS', 5, {headers: ['HTTP/1.1 400', 'x-test: ignored']})
  ]));
  assert.deepEqual(result.hosts['api.example'].response_status_lines, ['HTTP/1.1 400']);
  assert.equal(result.requests[0].hops[0].method, 'GET');
});

test('DNS and TCP events do not masquerade as HTTP requests', () => {
  const result = summarizeNetlog(log([
    event('HOST_RESOLVER_MANAGER_REQUEST', 1, {host: 'https://dns.example:443'}, 1, 2),
    event('TCP_CONNECT_ATTEMPT', 2, {address: '192.0.2.1:443'}, 1, 3),
    event('SSL_CONNECT', 2, {net_error: -202}, 2, 3)
  ]));
  assert.deepEqual(result.hosts, {});
  assert.equal(result.dns_requests.length, 1);
  assert.equal(result.tcp_connect_attempts.length, 1);
  assert.equal(result.tls_results[0].net_error, -202);
});

test('source type is part of request identity', () => {
  const result = summarizeNetlog(log([
    event('URL_REQUEST_START_JOB', 5, {url: 'https://one.example/'}, 1, 1),
    event('URL_REQUEST_START_JOB', 5, {url: 'https://two.example/'}, 1, 2)
  ]));
  assert.equal(result.requests.length, 2);
});

test('redirect notification is not counted as a second HTTP request', () => {
  const result = summarizeNetlog(log([
    event('URL_REQUEST_START_JOB', 5, {url: 'https://one.example/'}, 1),
    event('URL_REQUEST_REDIRECTED', 5, {location: 'https://two.example/'}),
    event('URL_REQUEST_START_JOB', 5, {url: 'https://two.example/'}, 1)
  ]));
  assert.equal(result.hosts['one.example'].requests, 1);
  assert.equal(result.hosts['two.example'].requests, 1);
});

test('redirect response status belongs only to its own host', () => {
  const result = summarizeNetlog(log([
    event('URL_REQUEST_START_JOB', 5, {url: 'https://one.example/'}, 1),
    event('HTTP_TRANSACTION_READ_RESPONSE_HEADERS', 5, {headers: ['HTTP/1.1 302 Found']}),
    event('URL_REQUEST_REDIRECTED', 5, {location: 'https://two.example/'}),
    event('URL_REQUEST_START_JOB', 5, {url: 'https://two.example/'}, 1),
    event('HTTP_TRANSACTION_READ_RESPONSE_HEADERS', 5, {headers: ['HTTP/1.1 200 OK']})
  ]));
  assert.deepEqual(result.hosts['one.example'].response_status_lines, ['HTTP/1.1 302 Found']);
  assert.deepEqual(result.hosts['two.example'].response_status_lines, ['HTTP/1.1 200 OK']);
});

/**
 * GRID PULSE - a minimal DOM for exercising the browser UI under node.
 *
 * WHY THIS EXISTS
 * ---------------
 * The Mode B user interface is a real deliverable, so "it probably works" is not
 * good enough. This harness implements exactly the slice of the DOM that web/ui/,
 * web/transport/local.js and web/play.html actually use, which lets ui_test.js boot
 * the application, synthesise keydown events, advance a controlled clock through a
 * full 60-second run, and assert on what the results screen ends up showing.
 *
 * It catches the class of bug that unit tests on the game core cannot: a mistyped
 * element id, a handler wired to the wrong event, a readout never updated, a results
 * field left blank.
 *
 * SCOPE AND HONESTY
 * -----------------
 * This is not a browser. It has no CSS engine, no layout, and no rendering, so it
 * proves the application's LOGIC and WIRING are correct - not that the page looks
 * right or that an animation plays. Visual correctness is verified by opening
 * web/play.html in a real browser; this covers everything underneath that.
 *
 * Element ids and the element tree are read out of web/play.html itself rather than
 * hardcoded, so if the markup and the controller drift apart, the tests fail.
 *
 * Test infrastructure only. Nothing here ships to the browser.
 */
'use strict';

const fs = require('fs');
const path = require('path');

/** A DOM-ish element: attributes, dataset, children, listeners. */
class FakeElement {
  constructor(tagName, ownerDocument) {
    this.tagName = String(tagName).toUpperCase();
    this.ownerDocument = ownerDocument;
    this.children = [];
    this.parentNode = null;
    this.attributes = Object.create(null);
    this.dataset = Object.create(null);
    this.style = Object.create(null);
    this.listeners = Object.create(null);
    this._textContent = '';
    this.innerHTML = '';
    this.className = '';
    this.hidden = false;
    this.disabled = false;
    this.value = '';
    // Any non-zero value; app code reads this only to force a style flush.
    this.offsetWidth = 100;
    this.classList = {
      _set: new Set(),
      add: (c) => { this.classList._set.add(c); },
      remove: (c) => { this.classList._set.delete(c); },
      contains: (c) => this.classList._set.has(c)
    };
  }

  get id() { return this.attributes.id || ''; }
  set id(v) { this.attributes.id = v; }

  get textContent() { return this._textContent; }
  set textContent(v) {
    this._textContent = String(v);
    if (v === '') { this.children.length = 0; }
  }

  setAttribute(name, value) {
    this.attributes[name] = String(value);
    if (name.startsWith('data-')) {
      this.dataset[dashToCamel(name.slice(5))] = String(value);
    }
    if (name === 'id' && this.ownerDocument) { this.ownerDocument._register(this); }
  }

  getAttribute(name) {
    return Object.prototype.hasOwnProperty.call(this.attributes, name)
      ? this.attributes[name] : null;
  }

  removeAttribute(name) {
    delete this.attributes[name];
    if (name.startsWith('data-')) { delete this.dataset[dashToCamel(name.slice(5))]; }
  }

  appendChild(node) {
    if (node instanceof FakeFragment) {
      node.children.forEach((child) => this.appendChild(child));
      node.children.length = 0;
      return node;
    }
    node.parentNode = this;
    this.children.push(node);
    return node;
  }

  removeChild(node) {
    const i = this.children.indexOf(node);
    if (i >= 0) { this.children.splice(i, 1); node.parentNode = null; }
    return node;
  }

  addEventListener(type, handler) {
    (this.listeners[type] || (this.listeners[type] = [])).push(handler);
  }

  removeEventListener(type, handler) {
    const list = this.listeners[type];
    if (!list) { return; }
    const i = list.indexOf(handler);
    if (i >= 0) { list.splice(i, 1); }
  }

  dispatchEvent(event) {
    const list = this.listeners[event.type];
    if (list) { list.slice().forEach((h) => h(event)); }
    return !event.defaultPrevented;
  }

  /** Convenience for tests: fire a click. */
  click() { this.dispatchEvent({ type: 'click', target: this }); }

  focus() {}
  select() {}

  getBoundingClientRect() { return { width: 900, height: 90, top: 0, left: 0 }; }

  /** Canvas support: a 2D context that records nothing but accepts everything. */
  getContext(kind) {
    if (kind !== '2d') { return null; }
    if (!this._ctx) {
      const noop = () => {};
      this._ctx = {
        canvas: this,
        setTransform: noop, clearRect: noop, beginPath: noop, moveTo: noop,
        lineTo: noop, stroke: noop, fill: noop, setLineDash: noop, arc: noop,
        lineWidth: 1, strokeStyle: '', fillStyle: '', lineJoin: '', lineCap: ''
      };
    }
    return this._ctx;
  }

  /** Depth-first search used by the document's id index. */
  * walk() {
    yield this;
    for (const child of this.children) { yield* child.walk(); }
  }
}

class FakeFragment extends FakeElement {
  constructor(ownerDocument) { super('#fragment', ownerDocument); }
}

function dashToCamel(s) {
  return s.replace(/-([a-z])/g, (_, c) => c.toUpperCase());
}

class FakeDocument {
  constructor() {
    this._byId = Object.create(null);
    this.listeners = Object.create(null);
    this.readyState = 'complete';
    this.hidden = false;
    this.body = new FakeElement('body', this);
  }

  _register(el) { if (el.id) { this._byId[el.id] = el; } }

  getElementById(id) { return this._byId[id] || null; }

  createElement(tag) { return new FakeElement(tag, this); }

  createDocumentFragment() { return new FakeFragment(this); }

  addEventListener(type, handler) {
    (this.listeners[type] || (this.listeners[type] = [])).push(handler);
  }

  removeEventListener(type, handler) {
    const list = this.listeners[type];
    if (!list) { return; }
    const i = list.indexOf(handler);
    if (i >= 0) { list.splice(i, 1); }
  }

  dispatchEvent(event) {
    const list = this.listeners[event.type];
    if (list) { list.slice().forEach((h) => h(event)); }
    return !event.defaultPrevented;
  }
}

/**
 * Build a document whose ids come from a real HTML file.
 *
 * Deliberately not a full HTML parser: it scrapes id attributes and the tag they sit
 * on. That is enough for getElementById, which is the only lookup the application
 * performs, and it means a renamed or deleted id in play.html breaks the tests
 * immediately - which is the property worth having.
 */
function documentFromHtml(htmlPath) {
  const html = fs.readFileSync(htmlPath, 'utf8');
  const doc = new FakeDocument();

  // The <body> tag carries the initial screen state as a data attribute, and the
  // application relies on that rather than setting it in JS. Reflect it, or the
  // tests would be checking a body the real page never has.
  const bodyTag = /<body\b([^>]*)>/i.exec(html);
  if (bodyTag) {
    const attrs = /([\w-]+)="([^"]*)"/g;
    let attr;
    while ((attr = attrs.exec(bodyTag[1])) !== null) {
      doc.body.setAttribute(attr[1], attr[2]);
    }
  }

  const tagWithId = /<([a-zA-Z][\w-]*)\b[^>]*\bid="([^"]+)"[^>]*>/g;
  let match;
  const found = [];
  while ((match = tagWithId.exec(html)) !== null) {
    const [, tag, id] = match;
    const el = doc.createElement(tag);
    el.setAttribute('id', id);
    doc.body.appendChild(el);
    found.push(id);
  }
  if (found.length === 0) {
    throw new Error('no ids found in ' + htmlPath + ' - is the file empty?');
  }
  doc._htmlIds = found;
  doc._html = html;
  return doc;
}

/** A clock and frame scheduler the test drives by hand. */
class FakeClock {
  constructor(startMs = 1000) {
    this.nowMs = startMs;
    this.frameCallbacks = [];
    this.timeouts = [];
    this.nextHandle = 1;
  }

  now() { return this.nowMs; }

  /** Advance time without running frames - used to model an unobserved gap. */
  advance(ms) {
    this.nowMs += ms;
    this._runDueTimeouts();
  }

  requestAnimationFrame(cb) {
    const handle = this.nextHandle++;
    this.frameCallbacks.push({ handle, cb });
    return handle;
  }

  cancelAnimationFrame(handle) {
    this.frameCallbacks = this.frameCallbacks.filter((f) => f.handle !== handle);
  }

  setTimeout(fn, ms) {
    const handle = this.nextHandle++;
    this.timeouts.push({ handle, fn, dueMs: this.nowMs + (ms || 0) });
    return handle;
  }

  clearTimeout(handle) {
    this.timeouts = this.timeouts.filter((t) => t.handle !== handle);
  }

  _runDueTimeouts() {
    for (;;) {
      const due = this.timeouts.filter((t) => t.dueMs <= this.nowMs);
      if (due.length === 0) { return; }
      this.timeouts = this.timeouts.filter((t) => t.dueMs > this.nowMs);
      due.forEach((t) => t.fn());
    }
  }

  /** Advance by one frame and run whatever was scheduled for it. */
  frame(stepMs = 16) {
    this.advance(stepMs);
    const pending = this.frameCallbacks;
    this.frameCallbacks = [];
    pending.forEach((f) => f.cb(this.nowMs));
  }

  /** Run frames until `predicate` is true or the budget is exhausted. */
  runUntil(predicate, { stepMs = 16, maxFrames = 20000 } = {}) {
    for (let i = 0; i < maxFrames; i++) {
      if (predicate()) { return true; }
      this.frame(stepMs);
    }
    return predicate();
  }
}

/**
 * Install the harness as the process globals and load the application scripts in
 * the same order web/play.html does.
 *
 * @returns {{GP: Object, doc: FakeDocument, clock: FakeClock, keyEvents: Array}}
 */
function bootApplication({ webRoot, htmlFile = 'play.html' } = {}) {
  const root = webRoot || path.join(__dirname, '..');
  const doc = documentFromHtml(path.join(root, htmlFile));
  const clock = new FakeClock();

  const windowListeners = Object.create(null);

  // Some of these (navigator, performance) are getter-only globals in recent node
  // versions, so plain assignment throws. defineProperty replaces them regardless.
  const define = (name, value) => {
    Object.defineProperty(globalThis, name, {
      value, writable: true, configurable: true, enumerable: true
    });
  };

  define('document', doc);
  define('performance', { now: () => clock.now() });
  define('requestAnimationFrame', (cb) => clock.requestAnimationFrame(cb));
  define('cancelAnimationFrame', (h) => clock.cancelAnimationFrame(h));
  define('setTimeout', (fn, ms) => clock.setTimeout(fn, ms));
  define('clearTimeout', (h) => clock.clearTimeout(h));
  define('devicePixelRatio', 2);
  define('location', { protocol: 'file:', href: 'file:///gridpulse/web/play.html' });
  define('navigator', { userAgent: 'gridpulse-dom-harness' });
  // Mode A's transport. Deliberately does NOT open by itself: whether a command may
  // be sent before the stream is live is exactly the ordering under test, so the test
  // decides when `open` fires. Mode B must never construct one - assert that with
  // eventSources().length rather than by making the constructor throw.
  const eventSources = [];
  function FakeEventSource(url) {
    this.url = url;
    this.readyState = 0;
    this.onopen = null;
    this.onerror = null;
    this.onmessage = null;
    this.closed = false;
    this._listeners = Object.create(null);
    eventSources.push(this);
  }
  FakeEventSource.prototype.addEventListener = function (type, handler) {
    (this._listeners[type] || (this._listeners[type] = [])).push(handler);
  };
  FakeEventSource.prototype.removeEventListener = function (type, handler) {
    const list = this._listeners[type];
    if (!list) { return; }
    const i = list.indexOf(handler);
    if (i >= 0) { list.splice(i, 1); }
  };
  FakeEventSource.prototype.close = function () { this.closed = true; };
  /** Fire `open`, as the browser does when the response headers arrive. */
  FakeEventSource.prototype.fireOpen = function () {
    this.readyState = 1;
    if (this.onopen) { this.onopen({ type: 'open' }); }
    (this._listeners.open || []).slice().forEach((h) => h({ type: 'open' }));
  };
  /** Deliver one server event, already JSON-encoded as the wire carries it. */
  FakeEventSource.prototype.fireMessage = function (payload) {
    const message = { data: JSON.stringify(payload) };
    if (this.onmessage) { this.onmessage(message); }
    (this._listeners.message || []).slice().forEach((h) => h(message));
  };
  define('EventSource', FakeEventSource);

  // Command transport. Records every POST so a test can assert not just what was
  // sent but when, relative to the stream opening.
  const commands = [];
  define('fetch', (url, options) => {
    const request = { url, options: options || {} };
    if (request.options.body) {
      try {
        const parsed = JSON.parse(request.options.body);
        request.name = parsed.name;
        request.args = parsed.args;
      } catch (err) { /* leave the raw body for the test to inspect */ }
    }
    commands.push(request);
    return Promise.resolve({
      ok: true,
      status: 200,
      json: () => Promise.resolve({ ok: true })
    });
  });

  globalThis.__eventSources = eventSources;
  globalThis.__commands = commands;
  define('focus', () => {});

  // An in-memory localStorage. node exposes an experimental one that prints a warning
  // on first touch, and the application legitimately reads a preference from it, so
  // the suite would emit that warning on every run. This is also closer to a browser
  // than node's is, and lets a test assert that a preference survives.
  const stored = new Map();
  define('localStorage', {
    getItem: (k) => (stored.has(k) ? stored.get(k) : null),
    setItem: (k, v) => { stored.set(k, String(v)); },
    removeItem: (k) => { stored.delete(k); },
    clear: () => { stored.clear(); }
  });

  globalThis.addEventListener = (type, handler) => {
    (windowListeners[type] || (windowListeners[type] = [])).push(handler);
  };
  globalThis.removeEventListener = (type, handler) => {
    const list = windowListeners[type];
    if (!list) { return; }
    const i = list.indexOf(handler);
    if (i >= 0) { list.splice(i, 1); }
  };
  // A real keydown reaches window-level capture listeners AND document-level
  // listeners. The application uses both - local.js listens on the window in capture
  // phase, app.js listens on the document for Escape - so the harness must deliver to
  // both or it would silently skip half the input wiring.
  globalThis.dispatchWindowEvent = (event) => {
    const list = windowListeners[event.type];
    if (list) { list.slice().forEach((h) => h(event)); }
    doc.dispatchEvent(event);
    return event;
  };
  globalThis.windowListenerCount = (type) =>
    (windowListeners[type] ? windowListeners[type].length : 0);

  // Fresh module state, in load order, exactly as play.html declares it.
  const scripts = [
    'core/rng.js', 'core/scoring.js', 'core/alphabet.js', 'core/boardmap.js',
    'core/session.js', 'ui/grid.js', 'ui/fx.js', 'ui/sound.js', 'ui/hud.js',
    'ui/sparkline.js',
    'ui/calibrate.js', 'transport/local.js', 'transport/sse.js', 'ui/app.js'
  ];
  delete globalThis.GridPulse;
  scripts.forEach((rel) => {
    const abs = path.join(root, rel);
    delete require.cache[require.resolve(abs)];
    require(abs);
  });

  return { GP: globalThis.GridPulse, doc, clock, scripts };
}

/**
 * Synthesise a keydown on a physical key.
 * Mirrors the real event shape the application relies on: `code`, `repeat`,
 * modifier flags, and a preventDefault that records whether it was called.
 */
function keyDown(code, { repeat = false, ctrlKey = false, metaKey = false,
                         altKey = false } = {}) {
  const event = {
    type: 'keydown',
    code,
    key: code.startsWith('Key') ? code.slice(3).toLowerCase() : code,
    repeat,
    ctrlKey,
    metaKey,
    altKey,
    defaultPrevented: false,
    preventDefault() { this.defaultPrevented = true; }
  };
  globalThis.dispatchWindowEvent(event);
  return event;
}

/** Every EventSource the application has constructed, in order. */
function eventSources() { return globalThis.__eventSources || []; }

/** Every command POST the application has made, in order. */
function commands() { return globalThis.__commands || []; }

module.exports = {
  FakeElement, FakeDocument, FakeClock,
  documentFromHtml, bootApplication, keyDown,
  eventSources, commands
};

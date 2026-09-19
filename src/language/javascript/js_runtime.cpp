#include "guchho/javascript/js_runtime.hpp"

#include <string>
#include <utility>

namespace guchho::javascript {

    // Returns a synthetic JavaScript source containing runtime helper
    // functions that the compiler emits when lowering modern syntax.
    // Each helper is selected per-file based on which JS features are
    // unsupported in the target environment.
    //
    // The helpers are intentionally given non-conflicting names (e.g.
    // __defProp instead of __assign) so that bundling Guchho output with
    // TypeScript-compiled code does not cause global-scope collisions.
    //
    // Input:  unsupported_js_features — bitset of features the target lacks
    // Output: logger::Source whose .contents is the full JS runtime text
    logger::Source Source(compat::JSFeature unsupported_js_features) {
        std::string text = R"JS_RUNTIME(
		var __create = Object.create
		var __freeze = Object.freeze
		var __defProp = Object.defineProperty
		var __defProps = Object.defineProperties
		var __getOwnPropDesc = Object.getOwnPropertyDescriptor
		var __getOwnPropDescs = Object.getOwnPropertyDescriptors
		var __getOwnPropNames = Object.getOwnPropertyNames
		var __getOwnPropSymbols = Object.getOwnPropertySymbols
		var __getProtoOf = Object.getPrototypeOf
		var __hasOwnProp = Object.prototype.hasOwnProperty
		var __propIsEnum = Object.prototype.propertyIsEnumerable
		var __reflectGet = Reflect.get
		var __reflectSet = Reflect.set

		var __knownSymbol = (name, symbol) => (symbol = Symbol[name]) ? symbol : Symbol.for('Symbol.' + name)
		var __typeError = msg => { throw TypeError(msg) }

		export var __pow = Math.pow

		var __defNormalProp = (obj, key, value) => key in obj
			? __defProp(obj, key, {enumerable: true, configurable: true, writable: true, value})
			: obj[key] = value

		export var __spreadValues = (a, b) => {
			for (var prop in b ||= {})
				if (__hasOwnProp.call(b, prop))
					__defNormalProp(a, prop, b[prop])
			if (__getOwnPropSymbols)
		)JS_RUNTIME";
        if (!compat::Has(unsupported_js_features, compat::JSFeature::kForOf)) {
            text += R"JS_RUNTIME(
				for (var prop of __getOwnPropSymbols(b)) {
		)JS_RUNTIME";
        } else {
            text += R"JS_RUNTIME(
				for (var props = __getOwnPropSymbols(b), i = 0, n = props.length, prop; i < n; i++) {
					prop = props[i]
		)JS_RUNTIME";
        }
        text += R"JS_RUNTIME(
					if (__propIsEnum.call(b, prop))
						__defNormalProp(a, prop, b[prop])
				}
			return a
		}
		export var __spreadProps = (a, b) => __defProps(a, __getOwnPropDescs(b))

		export var __name = (target, value) => __defProp(target, 'name', { value, configurable: true })

		export var __require =
			/* @__PURE__ */ (x =>
				typeof require !== 'undefined' ? require :
				typeof Proxy !== 'undefined' ? new Proxy(x, {
					get: (a, b) => (typeof require !== 'undefined' ? require : a)[b]
				}) : x
			)(function(x) {
				if (typeof require !== 'undefined') return require.apply(this, arguments)
				throw Error('Dynamic require of "' + x + '" is not supported')
			})

		export var __glob = map => path => {
			var fn = map[path]
			if (fn) return fn()
			throw new Error('Module not found in bundle: ' + path)
		}

		export var __restKey = key => typeof key === 'symbol' ? key : key + ''
		export var __objRest = (source, exclude) => {
			var target = {}
			for (var prop in source)
				if (__hasOwnProp.call(source, prop) && exclude.indexOf(prop) < 0)
					target[prop] = source[prop]
			if (source != null && __getOwnPropSymbols)
	)JS_RUNTIME";
        if (!compat::Has(unsupported_js_features, compat::JSFeature::kForOf)) {
            text += R"JS_RUNTIME(
				for (var prop of __getOwnPropSymbols(source)) {
		)JS_RUNTIME";
        } else {
            text += R"JS_RUNTIME(
				for (var props = __getOwnPropSymbols(source), i = 0, n = props.length, prop; i < n; i++) {
					prop = props[i]
		)JS_RUNTIME";
        }
        text += R"JS_RUNTIME(
					if (exclude.indexOf(prop) < 0 && __propIsEnum.call(source, prop))
						target[prop] = source[prop]
				}
			return target
		}

		export var __esm = (fn, res, err) => function __init() {
			if (err) throw err[0]
			try {
				return fn && (res = (0, fn[__getOwnPropNames(fn)[0]])(fn = 0)), res
			} catch (e) {
				throw err = [e], e
			}
		}
		export var __esmMin = (fn, res, err) => () => {
			if (err) throw err[0]
			try {
				return fn && (res = fn(fn = 0)), res
			} catch (e) {
				throw err = [e], e
			}
		}

		export var __commonJS = (cb, mod) => function __require() {
			try {
				return mod || (0, cb[__getOwnPropNames(cb)[0]])((mod = { exports: {} }).exports, mod), mod.exports
			} catch (e) {
				throw mod = 0, e
			}
		}
		export var __commonJSMin = (cb, mod) => () => {
			try {
				return mod || cb((mod = { exports: {} }).exports, mod), mod.exports
			} catch (e) {
				throw mod = 0, e
			}
		}

		export var __export = (target, all) => {
			for (var name in all)
				__defProp(target, name, { get: all[name], enumerable: true })
		}

		var __copyProps = (to, from, except, desc) => {
			if (from && typeof from === 'object' || typeof from === 'function')
	)JS_RUNTIME";
        if (!compat::Has(unsupported_js_features, compat::JSFeature::kForOf) && !compat::Has(unsupported_js_features, compat::JSFeature::kConstAndLet)) {
            text += R"JS_RUNTIME(
				for (let key of __getOwnPropNames(from))
					if (!__hasOwnProp.call(to, key) && key !== except)
						__defProp(to, key, { get: () => from[key], enumerable: !(desc = __getOwnPropDesc(from, key)) || desc.enumerable })
		)JS_RUNTIME";
        } else {
            text += R"JS_RUNTIME(
				for (var keys = __getOwnPropNames(from), i = 0, n = keys.length, key; i < n; i++) {
					key = keys[i]
					if (!__hasOwnProp.call(to, key) && key !== except)
						__defProp(to, key, { get: (k => from[k]).bind(null, key), enumerable: !(desc = __getOwnPropDesc(from, key)) || desc.enumerable })
				}
		)JS_RUNTIME";
        }
        text += R"JS_RUNTIME(
			return to
		}

		export var __reExport = (target, mod, secondTarget) => (
			__copyProps(target, mod, 'default'),
			secondTarget && __copyProps(secondTarget, mod, 'default')
		)

		export var __toESM = (mod, isNodeMode, target) => (
			target = mod != null ? __create(__getProtoOf(mod)) : {},
			__copyProps(
				isNodeMode || !mod || !mod.__esModule
					? __defProp(target, 'default', { value: mod, enumerable: true })
					: target,
				mod)
		)

		export var __toCommonJS = mod => __copyProps(__defProp({}, '__esModule', { value: true }), mod)

		export var __decorateClass = (decorators, target, key, kind) => {
			var result = kind > 1 ? void 0 : kind ? __getOwnPropDesc(target, key) : target
			for (var i = decorators.length - 1, decorator; i >= 0; i--)
				if (decorator = decorators[i])
					result = (kind ? decorator(target, key, result) : decorator(result)) || result
			if (kind && result) __defProp(target, key, result)
			return result
		}
		export var __decorateParam = (index, decorator) => (target, key) => decorator(target, key, index)

		export var __decoratorStart = base => [, , , __create(base?.[__knownSymbol('metadata')] ?? null)]
		var __decoratorStrings = ['class', 'method', 'getter', 'setter', 'accessor', 'field', 'value', 'get', 'set']
		var __expectFn = fn => fn !== void 0 && typeof fn !== 'function' ? __typeError('Function expected') : fn
		var __decoratorContext = (kind, name, done, metadata, fns) => ({ kind: __decoratorStrings[kind], name, metadata, addInitializer: fn =>
			done._ ? __typeError('Already initialized') : fns.push(__expectFn(fn || null)) })
		export var __decoratorMetadata = (array, target) => __defNormalProp(target, __knownSymbol('metadata'), array[3])
		export var __runInitializers = (array, flags, self, value) => {
			for (var i = 0, fns = array[flags >> 1], n = fns && fns.length; i < n; i++) flags & 1 ? fns[i].call(self) : value = fns[i].call(self, value)
			return value
		}
		export var __decorateElement = (array, flags, name, decorators, target, extra) => {
			var fn, it, done, ctx, access, k = flags & 7, s = !!(flags & 8), p = !!(flags & 16)
			var j = k > 3 ? array.length + 1 : k ? s ? 1 : 2 : 0, key = __decoratorStrings[k + 5]
			var initializers = k > 3 && (array[j - 1] = []), extraInitializers = array[j] || (array[j] = [])
			var desc = k && (
				!p && !s && (target = target.prototype),
				k < 5 && (k > 3 || !p) &&
			)JS_RUNTIME";
        if (!compat::Has(unsupported_js_features, compat::JSFeature::kObjectExtensions) && !compat::Has(unsupported_js_features, compat::JSFeature::kObjectAccessors)) {
            text += R"JS_RUNTIME(__getOwnPropDesc(k < 4 ? target : { get [name]() { return __privateGet(this, extra) }, set [name](x) { return __privateSet(this, extra, x) } }, name))JS_RUNTIME";
        } else {
            text += R"JS_RUNTIME((k < 4 ? __getOwnPropDesc(target, name) : { get: () => __privateGet(this, extra), set: x => __privateSet(this, extra, x) }))JS_RUNTIME";
        }
        text += R"JS_RUNTIME(
			)
			k ? p && k < 4 && __name(extra, (k > 2 ? 'set ' : k > 1 ? 'get ' : '') + name) : __name(target, name)

			for (var i = decorators.length - 1; i >= 0; i--) {
				ctx = __decoratorContext(k, name, done = {}, array[3], extraInitializers)

				if (k) {
					ctx.static = s, ctx.private = p, access = ctx.access = { has: p ? x => __privateIn(target, x) : x => name in x }
					if (k ^ 3) access.get = p ? x => (k ^ 1 ? __privateGet : __privateMethod)(x, target, k ^ 4 ? extra : desc.get) : x => x[name]
					if (k > 2) access.set = p ? (x, y) => __privateSet(x, target, y, k ^ 4 ? extra : desc.set) : (x, y) => x[name] = y
				}

				it = (0, decorators[i])(k ? k < 4 ? p ? extra : desc[key] : k > 4 ? void 0 : { get: desc.get, set: desc.set } : target, ctx), done._ = 1

				if (k ^ 4 || it === void 0) __expectFn(it) && (k > 4 ? initializers.unshift(it) : k ? p ? extra = it : desc[key] = it : target = it)
				else if (typeof it !== 'object' || it === null) __typeError('Object expected')
				else __expectFn(fn = it.get) && (desc.get = fn), __expectFn(fn = it.set) && (desc.set = fn), __expectFn(fn = it.init) && initializers.unshift(fn)
			}

			return k || __decoratorMetadata(array, target),
				desc && __defProp(target, name, desc),
				p ? k ^ 4 ? extra : desc : target
		}

		export var __publicField = (obj, key, value) => (
			__defNormalProp(obj, typeof key !== 'symbol' ? key + '' : key, value)
		)
		var __accessCheck = (obj, member, msg) => (
			member.has(obj) || __typeError('Cannot ' + msg)
		)
		export var __privateIn = (member, obj) => (
			Object(obj) !== obj ? __typeError('Cannot use the "in" operator on this value') :
			member.has(obj)
		)
		export var __privateGet = (obj, member, getter) => (
			__accessCheck(obj, member, 'read from private field'),
			getter ? getter.call(obj) : member.get(obj)
		)
		export var __privateAdd = (obj, member, value) => (
			member.has(obj) ? __typeError('Cannot add the same private member more than once') :
			member instanceof WeakSet ? member.add(obj) : member.set(obj, value)
		)
		export var __privateSet = (obj, member, value, setter) => (
			__accessCheck(obj, member, 'write to private field'),
			setter ? setter.call(obj, value) : member.set(obj, value),
			value
		)
		export var __privateMethod = (obj, member, method) => (
			__accessCheck(obj, member, 'access private method'),
			method
		)
		export var __earlyAccess = (name) => {
			throw ReferenceError('Cannot access "' + name + '" before initialization')
		}
	)JS_RUNTIME";
        if (!compat::Has(unsupported_js_features, compat::JSFeature::kObjectAccessors)) {
            text += R"JS_RUNTIME(
			export var __privateWrapper = (obj, member, setter, getter) => ({
				set _(value) { __privateSet(obj, member, value, setter) },
				get _() { return __privateGet(obj, member, getter) },
			})
		)JS_RUNTIME";
        } else {
            text += R"JS_RUNTIME(
		export var __privateWrapper = (obj, member, setter, getter) => __defProp({}, '_', {
			set: value => __privateSet(obj, member, value, setter),
			get: () => __privateGet(obj, member, getter),
		})
		)JS_RUNTIME";
        }
        text += R"JS_RUNTIME(
		export var __superGet = (cls, obj, key) => __reflectGet(__getProtoOf(cls), key, obj)
		export var __superSet = (cls, obj, key, val) => (__reflectSet(__getProtoOf(cls), key, val, obj), val)
	)JS_RUNTIME";
        if (!compat::Has(unsupported_js_features, compat::JSFeature::kObjectAccessors)) {
            text += R"JS_RUNTIME(
			export var __superWrapper = (cls, obj, key) => ({
				get _() { return __superGet(cls, obj, key) },
				set _(val) { __superSet(cls, obj, key, val) },
			})
		)JS_RUNTIME";
        } else {
            text += R"JS_RUNTIME(
			export var __superWrapper = (cls, obj, key) => __defProp({}, '_', {
				get: () => __superGet(cls, obj, key),
				set: val => __superSet(cls, obj, key, val),
			})
		)JS_RUNTIME";
        }
        text += R"JS_RUNTIME(
		export var __template = (cooked, raw) => __freeze(__defProp(cooked, 'raw', { value: __freeze(raw || cooked.slice()) }))

		export var __async = (__this, __arguments, generator) => {
			return new Promise((resolve, reject) => {
				var fulfilled = value => {
					try {
						step(generator.next(value))
					} catch (e) {
						reject(e)
					}
				}
				var rejected = value => {
					try {
						step(generator.throw(value))
					} catch (e) {
						reject(e)
					}
				}
				var step = x => x.done ? resolve(x.value) : Promise.resolve(x.value).then(fulfilled, rejected)
				step((generator = generator.apply(__this, __arguments)).next())
			})
		}

		export var __await = function (promise, isYieldStar) {
			this[0] = promise
			this[1] = isYieldStar
		}
		export var __asyncGenerator = (__this, __arguments, generator) => {
			var resume = (k, v, yes, no) => {
				try {
					var x = generator[k](v), isAwait = (v = x.value) instanceof __await, done = x.done
					Promise.resolve(isAwait ? v[0] : v)
						.then(y => isAwait
							? resume(k === 'return' ? k : 'next', v[1] ? { done: y.done, value: y.value } : y, yes, no)
							: yes({ value: y, done }))
						.catch(e => resume('throw', e, yes, no))
				} catch (e) {
					no(e)
				}
			}, method = (k, call, wait, clear) => it[k] = x => (
				call = new Promise((yes, no, run) => (
					run = () => resume(k, x, yes, no),
					q ? q.then(run) : run())),
				clear = () => q === wait && (q = 0),
				q = wait = call.then(clear, clear),
				call
			), q, it = {}
			return generator = generator.apply(__this, __arguments),
				it[__knownSymbol('asyncIterator')] = () => it,
				method('next'),
				method('throw'),
				method('return'),
				it
		}
		export var __yieldStar = value => {
			var obj = value[__knownSymbol('asyncIterator')], isAwait = false, method, it = {}
			if (obj == null) {
				obj = value[__knownSymbol('iterator')]()
				method = k => it[k] = x => obj[k](x)
			} else {
				obj = obj.call(value)
				method = k => it[k] = v => {
					if (isAwait) {
						isAwait = false
						if (k === 'throw') throw v
						return v
					}
					isAwait = true
					return {
						done: false,
						value: new __await(new Promise(resolve => {
							var x = obj[k](v)
							if (!(x instanceof Object)) __typeError('Object expected')
							resolve(x)
						}), 1),
					}
				}
			}
			return it[__knownSymbol('iterator')] = () => it,
				method('next'),
				'throw' in obj ? method('throw') : it.throw = x => { throw x },
				'return' in obj && method('return'),
				it
		}

		export var __forAwait = (obj, it, method) =>
			(it = obj[__knownSymbol('asyncIterator')])
				? it.call(obj)
				: (obj = obj[__knownSymbol('iterator')](),
					it = {},
					method = (key, fn) =>
						(fn = obj[key]) && (it[key] = arg =>
							new Promise((yes, no, done) => (
								arg = fn.call(obj, arg),
								done = arg.done,
								Promise.resolve(arg.value)
									.then(value => yes({ value, done }), no)
							))),
					method('next'),
					method('return'),
					it)

		export var __toBinaryNode = Uint8Array.fromBase64 || (base64 => new Uint8Array(Buffer.from(base64, 'base64')))
		export var __toBinary = Uint8Array.fromBase64 || /* @__PURE__ */ (() => {
			var table = new Uint8Array(128)
			for (var i = 0; i < 64; i++) table[i < 26 ? i + 65 : i < 52 ? i + 71 : i < 62 ? i - 4 : i * 4 - 205] = i
			return base64 => {
				var n = base64.length, bytes = new Uint8Array((n - (base64[n - 1] == '=') - (base64[n - 2] == '=')) * 3 / 4 | 0)
				for (var i = 0, j = 0; i < n;) {
					var c0 = table[base64.charCodeAt(i++)], c1 = table[base64.charCodeAt(i++)]
					var c2 = table[base64.charCodeAt(i++)], c3 = table[base64.charCodeAt(i++)]
					bytes[j++] = (c0 << 2) | (c1 >> 4)
					bytes[j++] = (c1 << 4) | (c2 >> 2)
					bytes[j++] = (c2 << 6) | c3
				}
				return bytes
			}
		})()

		export var __using = (stack, value, async) => {
			if (value != null) {
				if (typeof value !== 'object' && typeof value !== 'function') __typeError('Object expected')
				var dispose, inner
				if (async) dispose = value[__knownSymbol('asyncDispose')]
				if (dispose === void 0) {
					dispose = value[__knownSymbol('dispose')]
					if (async) inner = dispose
				}
				if (typeof dispose !== 'function') __typeError('Object not disposable')
				if (inner) dispose = function() { try { inner.call(this) } catch (e) { return Promise.reject(e) } }
				stack.push([async, dispose, value])
			} else if (async) {
				stack.push([async])
			}
			return value
		}
		export var __callDispose = (stack, error, hasError) => {
			var E = typeof SuppressedError === 'function' ? SuppressedError :
				function (e, s, m, _) { return _ = Error(m), _.name = 'SuppressedError', _.error = e, _.suppressed = s, _ }
			var fail = e => error = hasError ? new E(e, error, 'An error was suppressed during disposal') : (hasError = true, e)
			var next = (it) => {
				while (it = stack.pop()) {
					try {
						var result = it[1] && it[1].call(it[2])
						if (it[0]) return Promise.resolve(result).then(next, (e) => (fail(e), next()))
					} catch (e) {
						fail(e)
					}
				}
				if (hasError) throw error
			}
			return next()
		}
	)JS_RUNTIME";

        return logger::Source{
            .pretty_paths = logger::PrettyPaths{.abs = "<runtime>", .rel = "<runtime>"},
            .identifier_name = "runtime",
            .contents = std::move(text),
            .key_path = logger::Path{"<runtime>", {}, {}, {}, {}},
            .index = kSourceIndex,
        };
    }

}

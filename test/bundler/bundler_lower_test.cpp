#include "test/helpers/bundler_test.hpp"
#include "test/guchho_test.hpp"

namespace bundler::test {

Suite lower_suite{"lower"};

TEST(BundlerLower, LowerOptionalCatchNameCollisionNoBundle) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				try {}
				catch { var e, e2 }
				var e3
			
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::UnsupportedJSFeatures({{guchho::compat::Engine::kES, guchho::compat::Semver{.parts = {2018}}}}),
        },
    });
}

TEST(BundlerLower, LowerObjectSpreadNoBundle) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.jsx", R"pb(

				let tests = [
					{...a, ...b},
					{a, b, ...c},
					{...a, b, c},
					{a, ...b, c},
					{a, b, ...c, ...d, e, f, ...g, ...h, i, j},
				]
				let jsx = [
					<div {...a} {...b}/>,
					<div a b {...c}/>,
					<div {...a} b c/>,
					<div a {...b} c/>,
					<div a b {...c} {...d} e f {...g} {...h} i j/>,
				]
			
)pb"},
        },
        .entry_paths = {"/entry.jsx"},
        .options = guchho::config::Options{
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::UnsupportedJSFeatures({{guchho::compat::Engine::kES, guchho::compat::Semver{.parts = {2017}}}}),
        },
    });
}

TEST(BundlerLower, LowerExponentiationOperatorNoBundle) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				let tests = {
					// Exponentiation operator
					0: a ** b ** c,
					1: (a ** b) ** c,

					// Exponentiation assignment operator
					2: a **= b,
					3: a.b **= c,
					4: a[b] **= c,
					5: a().b **= c,
					6: a()[b] **= c,
					7: a[b()] **= c,
					8: a()[b()] **= c,

					// These all should not need capturing (no object identity)
					9: a[0] **= b,
					10: a[false] **= b,
					11: a[null] **= b,
					12: a[void 0] **= b,
					13: a[123n] **= b,
					14: a[this] **= b,

					// These should need capturing (have object identity)
					15: a[/x/] **= b,
					16: a[{}] **= b,
					17: a[[]] **= b,
					18: a[() => {}] **= b,
					19: a[function() {}] **= b,
				}
			
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::UnsupportedJSFeatures({{guchho::compat::Engine::kES, guchho::compat::Semver{.parts = {2015}}}}),
        },
        .expected_scan_log = R"esl(
entry.js: WARNING: Big integer literals are not available in the configured target environment and may crash at run-time
)esl",
    });
}

TEST(BundlerLower, LowerPrivateFieldAssignments2015NoBundle) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				class Foo {
					#x
					unary() {
						this.#x++
						this.#x--
						++this.#x
						--this.#x
					}
					binary() {
						this.#x = 1
						this.#x += 1
						this.#x -= 1
						this.#x *= 1
						this.#x /= 1
						this.#x %= 1
						this.#x **= 1
						this.#x <<= 1
						this.#x >>= 1
						this.#x >>>= 1
						this.#x &= 1
						this.#x |= 1
						this.#x ^= 1
						this.#x &&= 1
						this.#x ||= 1
						this.#x ??= 1
					}
				}
			
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
        	.AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::UnsupportedJSFeatures({{guchho::compat::Engine::kES, guchho::compat::Semver{.parts = {2015}}}}),
    
        },
    });
}

TEST(BundlerLower, LowerPrivateFieldAssignments2019NoBundle) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				class Foo {
					#x
					unary() {
						this.#x++
						this.#x--
						++this.#x
						--this.#x
					}
					binary() {
						this.#x = 1
						this.#x += 1
						this.#x -= 1
						this.#x *= 1
						this.#x /= 1
						this.#x %= 1
						this.#x **= 1
						this.#x <<= 1
						this.#x >>= 1
						this.#x >>>= 1
						this.#x &= 1
						this.#x |= 1
						this.#x ^= 1
						this.#x &&= 1
						this.#x ||= 1
						this.#x ??= 1
					}
				}
			
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::UnsupportedJSFeatures({{guchho::compat::Engine::kES, guchho::compat::Semver{.parts = {2019}}}}),
        },
    });
}

TEST(BundlerLower, LowerPrivateFieldAssignments2020NoBundle) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				class Foo {
					#x
					unary() {
						this.#x++
						this.#x--
						++this.#x
						--this.#x
					}
					binary() {
						this.#x = 1
						this.#x += 1
						this.#x -= 1
						this.#x *= 1
						this.#x /= 1
						this.#x %= 1
						this.#x **= 1
						this.#x <<= 1
						this.#x >>= 1
						this.#x >>>= 1
						this.#x &= 1
						this.#x |= 1
						this.#x ^= 1
						this.#x &&= 1
						this.#x ||= 1
						this.#x ??= 1
					}
				}
			
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::UnsupportedJSFeatures({{guchho::compat::Engine::kES, guchho::compat::Semver{.parts = {2020}}}}),
        },
    });
}

TEST(BundlerLower, LowerPrivateFieldAssignmentsNextNoBundle) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				class Foo {
					#x
					unary() {
						this.#x++
						this.#x--
						++this.#x
						--this.#x
					}
					binary() {
						this.#x = 1
						this.#x += 1
						this.#x -= 1
						this.#x *= 1
						this.#x /= 1
						this.#x %= 1
						this.#x **= 1
						this.#x <<= 1
						this.#x >>= 1
						this.#x >>>= 1
						this.#x &= 1
						this.#x |= 1
						this.#x ^= 1
						this.#x &&= 1
						this.#x ||= 1
						this.#x ??= 1
					}
				}
			
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerLower, LowerPrivateFieldOptionalChain2019NoBundle) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				class Foo {
					#x
					foo() {
						this?.#x.y
						this?.y.#x
						this.#x?.y
					}
				}
			
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::UnsupportedJSFeatures({{guchho::compat::Engine::kES, guchho::compat::Semver{.parts = {2019}}}}),
        },
    });
}

TEST(BundlerLower, LowerPrivateFieldOptionalChain2020NoBundle) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				class Foo {
					#x
					foo() {
						this?.#x.y
						this?.y.#x
						this.#x?.y
					}
				}
			
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::UnsupportedJSFeatures({{guchho::compat::Engine::kES, guchho::compat::Semver{.parts = {2020}}}}),
        },
    });
}

TEST(BundlerLower, LowerPrivateFieldOptionalChainNextNoBundle) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				class Foo {
					#x
					foo() {
						this?.#x.y
						this?.y.#x
						this.#x?.y
					}
				}
			
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerLower, TSLowerPrivateFieldOptionalChain2015NoBundle) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"pb(

				class Foo {
					#x
					foo() {
						this?.#x.y
						this?.y.#x
						this.#x?.y
					}
				}
			
)pb"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::UnsupportedJSFeatures({{guchho::compat::Engine::kES, guchho::compat::Semver{.parts = {2015}}}}),
        },
    });
}

TEST(BundlerLower, TSLowerPrivateStaticMembers2015NoBundle) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"pb(

				class Foo {
					static #x
					static get #y() {}
					static set #y(x) {}
					static #z() {}
					foo() {
						Foo.#x += 1
						Foo.#y += 1
						Foo.#z()
					}
				}
			
)pb"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::UnsupportedJSFeatures({{guchho::compat::Engine::kES, guchho::compat::Semver{.parts = {2015}}}}),
        },
    });
}

TEST(BundlerLower, TSLowerPrivateFieldAndMethodAvoidNameCollision2015) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"pb(

				export class WeakMap {
					#x
				}
				export class WeakSet {
					#y() {}
				}
			
)pb"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::UnsupportedJSFeatures({{guchho::compat::Engine::kES, guchho::compat::Semver{.parts = {2015}}}}),
        },
    });
}



TEST(BundlerLower, LowerPrivateGetterSetter2019) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				export class Foo {
					get #foo() { return this.foo }
					set #bar(val) { this.bar = val }
					get #prop() { return this.prop }
					set #prop(val) { this.prop = val }
					foo(fn) {
						fn().#foo
						fn().#bar = 1
						fn().#prop
						fn().#prop = 2
					}
					unary(fn) {
						fn().#prop++;
						fn().#prop--;
						++fn().#prop;
						--fn().#prop;
					}
					binary(fn) {
						fn().#prop = 1;
						fn().#prop += 1;
						fn().#prop -= 1;
						fn().#prop *= 1;
						fn().#prop /= 1;
						fn().#prop %= 1;
						fn().#prop **= 1;
						fn().#prop <<= 1;
						fn().#prop >>= 1;
						fn().#prop >>>= 1;
						fn().#prop &= 1;
						fn().#prop |= 1;
						fn().#prop ^= 1;
						fn().#prop &&= 1;
						fn().#prop ||= 1;
						fn().#prop ??= 1;
					}
				}
			
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::UnsupportedJSFeatures({{guchho::compat::Engine::kES, guchho::compat::Semver{.parts = {2019}}}}),
        },
    });
}

TEST(BundlerLower, LowerPrivateGetterSetter2020) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				export class Foo {
					get #foo() { return this.foo }
					set #bar(val) { this.bar = val }
					get #prop() { return this.prop }
					set #prop(val) { this.prop = val }
					foo(fn) {
						fn().#foo
						fn().#bar = 1
						fn().#prop
						fn().#prop = 2
					}
					unary(fn) {
						fn().#prop++;
						fn().#prop--;
						++fn().#prop;
						--fn().#prop;
					}
					binary(fn) {
						fn().#prop = 1;
						fn().#prop += 1;
						fn().#prop -= 1;
						fn().#prop *= 1;
						fn().#prop /= 1;
						fn().#prop %= 1;
						fn().#prop **= 1;
						fn().#prop <<= 1;
						fn().#prop >>= 1;
						fn().#prop >>>= 1;
						fn().#prop &= 1;
						fn().#prop |= 1;
						fn().#prop ^= 1;
						fn().#prop &&= 1;
						fn().#prop ||= 1;
						fn().#prop ??= 1;
					}
				}
			
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::UnsupportedJSFeatures({{guchho::compat::Engine::kES, guchho::compat::Semver{.parts = {2020}}}}),
        },
    });
}

TEST(BundlerLower, LowerPrivateGetterSetterNext) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				export class Foo {
					get #foo() { return this.foo }
					set #bar(val) { this.bar = val }
					get #prop() { return this.prop }
					set #prop(val) { this.prop = val }
					foo(fn) {
						fn().#foo
						fn().#bar = 1
						fn().#prop
						fn().#prop = 2
					}
					unary(fn) {
						fn().#prop++;
						fn().#prop--;
						++fn().#prop;
						--fn().#prop;
					}
					binary(fn) {
						fn().#prop = 1;
						fn().#prop += 1;
						fn().#prop -= 1;
						fn().#prop *= 1;
						fn().#prop /= 1;
						fn().#prop %= 1;
						fn().#prop **= 1;
						fn().#prop <<= 1;
						fn().#prop >>= 1;
						fn().#prop >>>= 1;
						fn().#prop &= 1;
						fn().#prop |= 1;
						fn().#prop ^= 1;
						fn().#prop &&= 1;
						fn().#prop ||= 1;
						fn().#prop ??= 1;
					}
				}
			
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerLower, LowerPrivateMethod2019) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				export class Foo {
					#field
					#method() {}
					baseline() {
						a().foo
						b().foo(x)
						c()?.foo(x)
						d().foo?.(x)
						e()?.foo?.(x)
					}
					privateField() {
						a().#field
						b().#field(x)
						c()?.#field(x)
						d().#field?.(x)
						e()?.#field?.(x)
						f()?.foo.#field(x).bar()
					}
					privateMethod() {
						a().#method
						b().#method(x)
						c()?.#method(x)
						d().#method?.(x)
						e()?.#method?.(x)
						f()?.foo.#method(x).bar()
					}
				}
			
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::UnsupportedJSFeatures({{guchho::compat::Engine::kES, guchho::compat::Semver{.parts = {2019}}}}),
        },
    });
}

TEST(BundlerLower, LowerPrivateMethod2020) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				export class Foo {
					#field
					#method() {}
					baseline() {
						a().foo
						b().foo(x)
						c()?.foo(x)
						d().foo?.(x)
						e()?.foo?.(x)
					}
					privateField() {
						a().#field
						b().#field(x)
						c()?.#field(x)
						d().#field?.(x)
						e()?.#field?.(x)
						f()?.foo.#field(x).bar()
					}
					privateMethod() {
						a().#method
						b().#method(x)
						c()?.#method(x)
						d().#method?.(x)
						e()?.#method?.(x)
						f()?.foo.#method(x).bar()
					}
				}
			
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::UnsupportedJSFeatures({{guchho::compat::Engine::kES, guchho::compat::Semver{.parts = {2020}}}}),
        },
    });
}

TEST(BundlerLower, LowerPrivateMethodNext) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				export class Foo {
					#field
					#method() {}
					baseline() {
						a().foo
						b().foo(x)
						c()?.foo(x)
						d().foo?.(x)
						e()?.foo?.(x)
					}
					privateField() {
						a().#field
						b().#field(x)
						c()?.#field(x)
						d().#field?.(x)
						e()?.#field?.(x)
						f()?.foo.#field(x).bar()
					}
					privateMethod() {
						a().#method
						b().#method(x)
						c()?.#method(x)
						d().#method?.(x)
						e()?.#method?.(x)
						f()?.foo.#method(x).bar()
					}
				}
			
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerLower, LowerPrivateClassExpr2020NoBundle) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				export let Foo = class {
					#field
					#method() {}
					static #staticField
					static #staticMethod() {}
					foo() {
						this.#field = this.#method()
						Foo.#staticField = Foo.#staticMethod()
					}
				}
			
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::UnsupportedJSFeatures({{guchho::compat::Engine::kES, guchho::compat::Semver{.parts = {2020}}}}),
        },
    });
}

TEST(BundlerLower, LowerPrivateMethodWithModifiers2020) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				export class Foo {
					*#g() {}
					async #a() {}
					async *#ag() {}

					static *#sg() {}
					static async #sa() {}
					static async *#sag() {}
				}
			
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::UnsupportedJSFeatures({{guchho::compat::Engine::kES, guchho::compat::Semver{.parts = {2020}}}}),
        },
    });
}

TEST(BundlerLower, LowerAsync2016NoBundle) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				async function foo(bar) {
					await bar
					return [this, arguments]
				}
				class Foo {async foo() {}}
				new (class Bar extends class { } {
					constructor() {
						let x = 1;
						(async () => {
							console.log("before super", x);  // (1) Sync phase
							await 1;
							console.log("after super", x);   // (2) Async phase
						})();
						super();
						x = 2;
					}
				})();
				export default [
					foo,
					Foo,
					async function() {},
					async () => {},
					{async foo() {}},
					class {async foo() {}},
					function() {
						return async (bar) => {
							await bar
							return [this, arguments]
						}
					},
				]
			
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::UnsupportedJSFeatures({{guchho::compat::Engine::kES, guchho::compat::Semver{.parts = {2016}}}}),
        },
    });
}

TEST(BundlerLower, LowerAsync2017NoBundle) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				async function foo(bar) {
					await bar
					return arguments
				}
				class Foo {async foo() {}}
				export default [
					foo,
					Foo,
					async function() {},
					async () => {},
					{async foo() {}},
					class {async foo() {}},
					function() {
						return async (bar) => {
							await bar
							return [this, arguments]
						}
					},
				]
			
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::UnsupportedJSFeatures({{guchho::compat::Engine::kES, guchho::compat::Semver{.parts = {2017}}}}),
        },
    });
}

TEST(BundlerLower, LowerAsyncThis2016CommonJS) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				exports.foo = async () => this
			
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::UnsupportedJSFeatures({{guchho::compat::Engine::kES, guchho::compat::Semver{.parts = {2016}}}}),
        },
    });
}

TEST(BundlerLower, LowerAsyncThis2016ES6) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				export {bar} from "./other"
				export let foo = async () => this
			
)pb"},
            {"/other.js", R"pb(

				export let bar = async () => {}
			
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::UnsupportedJSFeatures({{guchho::compat::Engine::kES, guchho::compat::Semver{.parts = {2016}}}}),
        },
		.expected_scan_log = R"esl(
entry.js: DEBUG: Top-level "this" will be replaced with undefined since this file is an ECMAScript module
entry.js: NOTE: This file is considered to be an ECMAScript module because of the "export" keyword here:
)esl",
        .debug_logs = true,
        
    });
}

TEST(BundlerLower, LowerAsyncES5) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				import './fn-stmt'
				import './fn-expr'
				import './arrow-1'
				import './arrow-2'
				import './export-def-1'
				import './export-def-2'
				import './obj-method'
			
)pb"},
            {"/fn-stmt.js", R"pb(
async function foo() {}
)pb"},
            {"/fn-expr.js", R"pb(
(async function() {})
)pb"},
            {"/arrow-1.js", R"pb(
(async () => {})
)pb"},
            {"/arrow-2.js", R"pb(
(async x => {})
)pb"},
            {"/export-def-1.js", R"pb(
export default async function foo() {}
)pb"},
            {"/export-def-2.js", R"pb(
export default async function() {}
)pb"},
            {"/obj-method.js", R"pb(
({async foo() {}})
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
			.UnsupportedJSFeatures = guchho::compat::UnsupportedJSFeatures({{guchho::compat::Engine::kES, guchho::compat::Semver{.parts = {5}}}}),
        },
        .expected_scan_log = R"esl(
arrow-1.js: ERROR: Transforming async functions to the configured target environment is not supported yet
arrow-2.js: ERROR: Transforming async functions to the configured target environment is not supported yet
export-def-1.js: ERROR: Transforming async functions to the configured target environment is not supported yet
export-def-2.js: ERROR: Transforming async functions to the configured target environment is not supported yet
fn-expr.js: ERROR: Transforming async functions to the configured target environment is not supported yet
fn-stmt.js: ERROR: Transforming async functions to the configured target environment is not supported yet
obj-method.js: ERROR: Transforming async functions to the configured target environment is not supported yet
)esl",
    });
}

TEST(BundlerLower, LowerAsyncSuperES2017NoBundle) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				class Derived extends Base {
					async test(key) {
						return [
							await super.foo,
							await super[key],
							await ([super.foo] = [0]),
							await ([super[key]] = [0]),

							await (super.foo = 1),
							await (super[key] = 1),
							await (super.foo += 2),
							await (super[key] += 2),

							await ++super.foo,
							await ++super[key],
							await super.foo++,
							await super[key]++,

							await super.foo.name,
							await super[key].name,
							await super.foo?.name,
							await super[key]?.name,

							await super.foo(1, 2),
							await super[key](1, 2),
							await super.foo?.(1, 2),
							await super[key]?.(1, 2),

							await (() => super.foo)(),
							await (() => super[key])(),
							await (() => super.foo())(),
							await (() => super[key]())(),

							await super.foo``,
							await super[key]``,
						]
					}
				}

				// This covers a bug that caused a compiler crash
				let fn = async () => class extends Base {
					a = super.a
					b = () => super.b
					c() { return super.c }
					d() { return () => super.d }
				}

				// This covers a bug that generated bad code
				class Derived2 extends Base {
					async a() { return class { [super.foo] = 123 } }
					b = async () => class { [super.foo] = 123 }
				}

				// This covers putting the generated temporary variable inside the loop
				for (let i = 0; i < 3; i++) {
					objs.push({
						__proto__: {
							foo() { return i },
						},
						async bar() { return super.foo() },
					})
				}
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::UnsupportedJSFeatures({{guchho::compat::Engine::kES, guchho::compat::Semver{.parts = {2017}}}}),
        },
    });
}



TEST(BundlerLower, LowerStaticSuperES2021NoBundle) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				class Derived extends Base {
					static test = key => {
						return [
							super.foo,
							super[key],
							([super.foo] = [0]),
							([super[key]] = [0]),

							(super.foo = 1),
							(super[key] = 1),
							(super.foo += 2),
							(super[key] += 2),

							++super.foo,
							++super[key],
							super.foo++,
							super[key]++,

							super.foo.name,
							super[key].name,
							super.foo?.name,
							super[key]?.name,

							super.foo(1, 2),
							super[key](1, 2),
							super.foo?.(1, 2),
							super[key]?.(1, 2),

							(() => super.foo)(),
							(() => super[key])(),
							(() => super.foo())(),
							(() => super[key]())(),

							super.foo``,
							super[key]``,
						]
					}
				}
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::UnsupportedJSFeatures({{guchho::compat::Engine::kES, guchho::compat::Semver{.parts = {2021}}}}),
        },
    });
}

TEST(BundlerLower, LowerStaticSuperES2016NoBundle) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				class Derived extends Base {
					static test = key => {
						return [
							super.foo,
							super[key],
							([super.foo] = [0]),
							([super[key]] = [0]),

							(super.foo = 1),
							(super[key] = 1),
							(super.foo += 2),
							(super[key] += 2),

							++super.foo,
							++super[key],
							super.foo++,
							super[key]++,

							super.foo.name,
							super[key].name,
							super.foo?.name,
							super[key]?.name,

							super.foo(1, 2),
							super[key](1, 2),
							super.foo?.(1, 2),
							super[key]?.(1, 2),

							(() => super.foo)(),
							(() => super[key])(),
							(() => super.foo())(),
							(() => super[key]())(),

							super.foo``,
							super[key]``,
						]
					}
				}
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::UnsupportedJSFeatures({{guchho::compat::Engine::kES, guchho::compat::Semver{.parts = {2016}}}}),
        },
    });
}

TEST(BundlerLower, LowerAsyncArrowSuperES2016) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				export { default as foo1 } from "./foo1"
				export { default as foo2 } from "./foo2"
				export { default as foo3 } from "./foo3"
				export { default as foo4 } from "./foo4"
				export { default as bar1 } from "./bar1"
				export { default as bar2 } from "./bar2"
				export { default as bar3 } from "./bar3"
				export { default as bar4 } from "./bar4"
				export { default as baz1 } from "./baz1"
				export { default as baz2 } from "./baz2"
				import "./outer"
			
)pb"},
            {"/foo1.js", R"pb(
export default class extends x { foo1() { return async () => super.foo('foo1') } }
)pb"},
            {"/foo2.js", R"pb(
export default class extends x { foo2() { return async () => () => super.foo('foo2') } }
)pb"},
            {"/foo3.js", R"pb(
export default class extends x { foo3() { return () => async () => super.foo('foo3') } }
)pb"},
            {"/foo4.js", R"pb(
export default class extends x { foo4() { return async () => async () => super.foo('foo4') } }
)pb"},
            {"/bar1.js", R"pb(
export default class extends x { bar1 = async () => super.foo('bar1') }
)pb"},
            {"/bar2.js", R"pb(
export default class extends x { bar2 = async () => () => super.foo('bar2') }
)pb"},
            {"/bar3.js", R"pb(
export default class extends x { bar3 = () => async () => super.foo('bar3') }
)pb"},
            {"/bar4.js", R"pb(
export default class extends x { bar4 = async () => async () => super.foo('bar4') }
)pb"},
            {"/baz1.js", R"pb(
export default class extends x { async baz1() { return () => super.foo('baz1') } }
)pb"},
            {"/baz2.js", R"pb(
export default class extends x { async baz2() { return () => () => super.foo('baz2') } }
)pb"},
            {"/outer.js", R"pb(

				// Helper functions for "super" shouldn't be inserted into this outer function
				export default (async function () {
					class y extends z {
						foo = async () => super.foo()
					}
					await new y().foo()()
				})()
			
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::UnsupportedJSFeatures({{guchho::compat::Engine::kES, guchho::compat::Semver{.parts = {2016}}}}),
        },
    });
}

TEST(BundlerLower, LowerAsyncArrowSuperSetterES2016) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				export { default as foo1 } from "./foo1"
				export { default as foo2 } from "./foo2"
				export { default as foo3 } from "./foo3"
				export { default as foo4 } from "./foo4"
				export { default as bar1 } from "./bar1"
				export { default as bar2 } from "./bar2"
				export { default as bar3 } from "./bar3"
				export { default as bar4 } from "./bar4"
				export { default as baz1 } from "./baz1"
				export { default as baz2 } from "./baz2"
				import "./outer"
			
)pb"},
            {"/foo1.js", R"pb(
export default class extends x { foo1() { return async () => super.foo = 'foo1' } }
)pb"},
            {"/foo2.js", R"pb(
export default class extends x { foo2() { return async () => () => super.foo = 'foo2' } }
)pb"},
            {"/foo3.js", R"pb(
export default class extends x { foo3() { return () => async () => super.foo = 'foo3' } }
)pb"},
            {"/foo4.js", R"pb(
export default class extends x { foo4() { return async () => async () => super.foo = 'foo4' } }
)pb"},
            {"/bar1.js", R"pb(
export default class extends x { bar1 = async () => super.foo = 'bar1' }
)pb"},
            {"/bar2.js", R"pb(
export default class extends x { bar2 = async () => () => super.foo = 'bar2' }
)pb"},
            {"/bar3.js", R"pb(
export default class extends x { bar3 = () => async () => super.foo = 'bar3' }
)pb"},
            {"/bar4.js", R"pb(
export default class extends x { bar4 = async () => async () => super.foo = 'bar4' }
)pb"},
            {"/baz1.js", R"pb(
export default class extends x { async baz1() { return () => super.foo = 'baz1' } }
)pb"},
            {"/baz2.js", R"pb(
export default class extends x { async baz2() { return () => () => super.foo = 'baz2' } }
)pb"},
            {"/outer.js", R"pb(

				// Helper functions for "super" shouldn't be inserted into this outer function
				export default (async function () {
					class y extends z {
						foo = async () => super.foo = 'foo'
					}
					await new y().foo()()
				})()
			
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::UnsupportedJSFeatures({{guchho::compat::Engine::kES, guchho::compat::Semver{.parts = {2016}}}}),
        },
    });
}

TEST(BundlerLower, LowerStaticAsyncArrowSuperES2016) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				export { default as foo1 } from "./foo1"
				export { default as foo2 } from "./foo2"
				export { default as foo3 } from "./foo3"
				export { default as foo4 } from "./foo4"
				export { default as bar1 } from "./bar1"
				export { default as bar2 } from "./bar2"
				export { default as bar3 } from "./bar3"
				export { default as bar4 } from "./bar4"
				export { default as baz1 } from "./baz1"
				export { default as baz2 } from "./baz2"
				import "./outer"
			
)pb"},
            {"/foo1.js", R"pb(
export default class extends x { static foo1() { return async () => super.foo('foo1') } }
)pb"},
            {"/foo2.js", R"pb(
export default class extends x { static foo2() { return async () => () => super.foo('foo2') } }
)pb"},
            {"/foo3.js", R"pb(
export default class extends x { static foo3() { return () => async () => super.foo('foo3') } }
)pb"},
            {"/foo4.js", R"pb(
export default class extends x { static foo4() { return async () => async () => super.foo('foo4') } }
)pb"},
            {"/bar1.js", R"pb(
export default class extends x { static bar1 = async () => super.foo('bar1') }
)pb"},
            {"/bar2.js", R"pb(
export default class extends x { static bar2 = async () => () => super.foo('bar2') }
)pb"},
            {"/bar3.js", R"pb(
export default class extends x { static bar3 = () => async () => super.foo('bar3') }
)pb"},
            {"/bar4.js", R"pb(
export default class extends x { static bar4 = async () => async () => super.foo('bar4') }
)pb"},
            {"/baz1.js", R"pb(
export default class extends x { static async baz1() { return () => super.foo('baz1') } }
)pb"},
            {"/baz2.js", R"pb(
export default class extends x { static async baz2() { return () => () => super.foo('baz2') } }
)pb"},
            {"/outer.js", R"pb(

				// Helper functions for "super" shouldn't be inserted into this outer function
				export default (async function () {
					class y extends z {
						static foo = async () => super.foo()
					}
					await y.foo()()
				})()
			
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::UnsupportedJSFeatures({{guchho::compat::Engine::kES, guchho::compat::Semver{.parts = {2016}}}}),
        },
    });
}

TEST(BundlerLower, LowerStaticAsyncArrowSuperSetterES2016) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				export { default as foo1 } from "./foo1"
				export { default as foo2 } from "./foo2"
				export { default as foo3 } from "./foo3"
				export { default as foo4 } from "./foo4"
				export { default as bar1 } from "./bar1"
				export { default as bar2 } from "./bar2"
				export { default as bar3 } from "./bar3"
				export { default as bar4 } from "./bar4"
				export { default as baz1 } from "./baz1"
				export { default as baz2 } from "./baz2"
				import "./outer"
			
)pb"},
            {"/foo1.js", R"pb(
export default class extends x { static foo1() { return async () => super.foo = 'foo1' } }
)pb"},
            {"/foo2.js", R"pb(
export default class extends x { static foo2() { return async () => () => super.foo = 'foo2' } }
)pb"},
            {"/foo3.js", R"pb(
export default class extends x { static foo3() { return () => async () => super.foo = 'foo3' } }
)pb"},
            {"/foo4.js", R"pb(
export default class extends x { static foo4() { return async () => async () => super.foo = 'foo4' } }
)pb"},
            {"/bar1.js", R"pb(
export default class extends x { static bar1 = async () => super.foo = 'bar1' }
)pb"},
            {"/bar2.js", R"pb(
export default class extends x { static bar2 = async () => () => super.foo = 'bar2' }
)pb"},
            {"/bar3.js", R"pb(
export default class extends x { static bar3 = () => async () => super.foo = 'bar3' }
)pb"},
            {"/bar4.js", R"pb(
export default class extends x { static bar4 = async () => async () => super.foo = 'bar4' }
)pb"},
            {"/baz1.js", R"pb(
export default class extends x { static async baz1() { return () => super.foo = 'baz1' } }
)pb"},
            {"/baz2.js", R"pb(
export default class extends x { static async baz2() { return () => () => super.foo = 'baz2' } }
)pb"},
            {"/outer.js", R"pb(

				// Helper functions for "super" shouldn't be inserted into this outer function
				export default (async function () {
					class y extends z {
						static foo = async () => super.foo = 'foo'
					}
					await y.foo()()
				})()
			
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::UnsupportedJSFeatures({{guchho::compat::Engine::kES, guchho::compat::Semver{.parts = {2016}}}}),
        },
    });
}

TEST(BundlerLower, LowerPrivateSuperES2022) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				export { default as foo1 } from "./foo1"
				export { default as foo2 } from "./foo2"
				export { default as foo3 } from "./foo3"
				export { default as foo4 } from "./foo4"
				export { default as foo5 } from "./foo5"
				export { default as foo6 } from "./foo6"
				export { default as foo7 } from "./foo7"
				export { default as foo8 } from "./foo8"
			
)pb"},
            {"/foo1.js", R"pb(
export default class extends x { #foo() { super.foo() } }
)pb"},
            {"/foo2.js", R"pb(
export default class extends x { #foo() { super.foo++ } }
)pb"},
            {"/foo3.js", R"pb(
export default class extends x { static #foo() { super.foo() } }
)pb"},
            {"/foo4.js", R"pb(
export default class extends x { static #foo() { super.foo++ } }
)pb"},
            {"/foo5.js", R"pb(
export default class extends x { #foo = () => { super.foo() } }
)pb"},
            {"/foo6.js", R"pb(
export default class extends x { #foo = () => { super.foo++ } }
)pb"},
            {"/foo7.js", R"pb(
export default class extends x { static #foo = () => { super.foo() } }
)pb"},
            {"/foo8.js", R"pb(
export default class extends x { static #foo = () => { super.foo++ } }
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::UnsupportedJSFeatures({{guchho::compat::Engine::kES, guchho::compat::Semver{.parts = {2022}}}}),
        },
    });
}

TEST(BundlerLower, LowerPrivateSuperES2021) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				export { default as foo1 } from "./foo1"
				export { default as foo2 } from "./foo2"
				export { default as foo3 } from "./foo3"
				export { default as foo4 } from "./foo4"
				export { default as foo5 } from "./foo5"
				export { default as foo6 } from "./foo6"
				export { default as foo7 } from "./foo7"
				export { default as foo8 } from "./foo8"
			
)pb"},
            {"/foo1.js", R"pb(
export default class extends x { #foo() { super.foo() } }
)pb"},
            {"/foo2.js", R"pb(
export default class extends x { #foo() { super.foo++ } }
)pb"},
            {"/foo3.js", R"pb(
export default class extends x { static #foo() { super.foo() } }
)pb"},
            {"/foo4.js", R"pb(
export default class extends x { static #foo() { super.foo++ } }
)pb"},
            {"/foo5.js", R"pb(
export default class extends x { #foo = () => { super.foo() } }
)pb"},
            {"/foo6.js", R"pb(
export default class extends x { #foo = () => { super.foo++ } }
)pb"},
            {"/foo7.js", R"pb(
export default class extends x { static #foo = () => { super.foo() } }
)pb"},
            {"/foo8.js", R"pb(
export default class extends x { static #foo = () => { super.foo++ } }
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::UnsupportedJSFeatures({{guchho::compat::Engine::kES, guchho::compat::Semver{.parts = {2021}}}}),
        },
    });
}

TEST(BundlerLower, LowerPrivateSuperStaticBundleIssue2158) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				export class Foo extends Object {
					static FOO;
					constructor() {
						super();
					}
					#foo;
				}
			
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerLower, LowerClassField2020NoBundle) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				class Foo {
					#foo = 123
					#bar
					foo = 123
					bar
					static #s_foo = 123
					static #s_bar
					static s_foo = 123
					static s_bar
				}
			
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::UnsupportedJSFeatures({{guchho::compat::Engine::kES, guchho::compat::Semver{.parts = {2020}}}}),
        },
    });
}

TEST(BundlerLower, LowerClassFieldNextNoBundle) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				class Foo {
					#foo = 123
					#bar
					foo = 123
					bar
					static #s_foo = 123
					static #s_bar
					static s_foo = 123
					static s_bar
				}
			
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerLower, TSLowerClassField2020NoBundle) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"pb(

				class Foo {
					#foo = 123
					#bar
					foo = 123
					bar
					static #s_foo = 123
					static #s_bar
					static s_foo = 123
					static s_bar
				}
			
)pb"},
            {"/tsconfig.json", R"pb(
{
				"compilerOptions": {
					"useDefineForClassFields": false
				}
			}
)pb"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::UnsupportedJSFeatures({{guchho::compat::Engine::kES, guchho::compat::Semver{.parts = {2020}}}}),
        },
    });
}

TEST(BundlerLower, TSLowerClassPrivateFieldNextNoBundle) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"pb(

				class Foo {
					#foo = 123
					#bar
					foo = 123
					bar
					static #s_foo = 123
					static #s_bar
					static s_foo = 123
					static s_bar
				}
			
)pb"},
            {"/tsconfig.json", R"pb(
{
				"compilerOptions": {
					"useDefineForClassFields": false
				}
			}
)pb"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerLower, LowerClassFieldStrictTsconfigJson2020) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				import loose from './loose'
				import strict from './strict'
				console.log(loose, strict)
			
)pb"},
            {"/loose/index.js", R"pb(

				export default class {
					foo
				}
			
)pb"},
            {"/loose/tsconfig.json", R"pb(

				{
					"compilerOptions": {
						"useDefineForClassFields": false
					}
				}
			
)pb"},
            {"/strict/index.js", R"pb(

				export default class {
					foo
				}
			
)pb"},
            {"/strict/tsconfig.json", R"pb(

				{
					"compilerOptions": {
						"useDefineForClassFields": true
					}
				}
			
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::UnsupportedJSFeatures({{guchho::compat::Engine::kES, guchho::compat::Semver{.parts = {2020}}}}),
        },
    });
}

TEST(BundlerLower, TSLowerClassFieldStrictTsconfigJson2020) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				import loose from './loose'
				import strict from './strict'
				console.log(loose, strict)
			
)pb"},
            {"/loose/index.ts", R"pb(

				export default class {
					foo
				}
			
)pb"},
            {"/loose/tsconfig.json", R"pb(

				{
					"compilerOptions": {
						"useDefineForClassFields": false
					}
				}
			
)pb"},
            {"/strict/index.ts", R"pb(

				export default class {
					foo
				}
			
)pb"},
            {"/strict/tsconfig.json", R"pb(

				{
					"compilerOptions": {
						"useDefineForClassFields": true
					}
				}
			
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::UnsupportedJSFeatures({{guchho::compat::Engine::kES, guchho::compat::Semver{.parts = {2020}}}}),
        },
    });
}

TEST(BundlerLower, TSLowerObjectRest2017NoBundle) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"pb(

				const { ...local_const } = {};
				let { ...local_let } = {};
				var { ...local_var } = {};
				let arrow_fn = ({ ...x }) => { };
				let fn_expr = function ({ ...x } = default_value) {};
				let class_expr = class { method(x, ...[y, { ...z }]) {} };

				function fn_stmt({ a = b(), ...x }, { c = d(), ...y }) {}
				class class_stmt { method({ ...x }) {} }
				namespace ns { export let { ...x } = {} }
				try { } catch ({ ...catch_clause }) {}

				for (const { ...for_in_const } in { abc }) {}
				for (let { ...for_in_let } in { abc }) {}
				for (var { ...for_in_var } in { abc }) ;
				for (const { ...for_of_const } of [{}]) ;
				for (let { ...for_of_let } of [{}]) x()
				for (var { ...for_of_var } of [{}]) x()
				for (const { ...for_const } = {}; x; x = null) {}
				for (let { ...for_let } = {}; x; x = null) {}
				for (var { ...for_var } = {}; x; x = null) {}
				for ({ ...x } in { abc }) {}
				for ({ ...x } of [{}]) {}
				for ({ ...x } = {}; x; x = null) {}

				({ ...assign } = {});
				({ obj_method({ ...x }) {} });

				// Check for used return values
				({ ...x } = x);
				for ({ ...x } = x; 0; ) ;
				console.log({ ...x } = x);
				console.log({ x, ...xx } = { x });
				console.log({ x: { ...xx } } = { x });
			
)pb"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::UnsupportedJSFeatures({{guchho::compat::Engine::kES, guchho::compat::Semver{.parts = {2017}}}}),
        },
    });
}

TEST(BundlerLower, TSLowerObjectRest2018NoBundle) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"pb(

				const { ...local_const } = {};
				let { ...local_let } = {};
				var { ...local_var } = {};
				let arrow_fn = ({ ...x }) => { };
				let fn_expr = function ({ ...x } = default_value) {};
				let class_expr = class { method(x, ...[y, { ...z }]) {} };

				function fn_stmt({ a = b(), ...x }, { c = d(), ...y }) {}
				class class_stmt { method({ ...x }) {} }
				namespace ns { export let { ...x } = {} }
				try { } catch ({ ...catch_clause }) {}

				for (const { ...for_in_const } in { abc }) {}
				for (let { ...for_in_let } in { abc }) {}
				for (var { ...for_in_var } in { abc }) ;
				for (const { ...for_of_const } of [{}]) ;
				for (let { ...for_of_let } of [{}]) x()
				for (var { ...for_of_var } of [{}]) x()
				for (const { ...for_const } = {}; x; x = null) {}
				for (let { ...for_let } = {}; x; x = null) {}
				for (var { ...for_var } = {}; x; x = null) {}
				for ({ ...x } in { abc }) {}
				for ({ ...x } of [{}]) {}
				for ({ ...x } = {}; x; x = null) {}

				({ ...assign } = {});
				({ obj_method({ ...x }) {} });

				// Check for used return values
				({ ...x } = x);
				for ({ ...x } = x; 0; ) ;
				console.log({ ...x } = x);
				console.log({ x, ...xx } = { x });
				console.log({ x: { ...xx } } = { x });
			
)pb"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::UnsupportedJSFeatures({{guchho::compat::Engine::kES, guchho::compat::Semver{.parts = {2018}}}}),
        },
    });
}

TEST(BundlerLower, ClassSuperThisIssue242NoBundle) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"pb(

				export class A {}

				export class B extends A {
					#e: string
					constructor(c: { d: any }) {
						super()
						this.#e = c.d ?? 'test'
					}
					f() {
						return this.#e
					}
				}
			
)pb"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::UnsupportedJSFeatures({{guchho::compat::Engine::kES, guchho::compat::Semver{.parts = {2019}}}}),
        },
    });
}

TEST(BundlerLower, LowerExportStarAsNameCollisionNoBundle) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				export * as ns from 'path'
				let ns = 123
				export {ns as sn}
			
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::UnsupportedJSFeatures({{guchho::compat::Engine::kES, guchho::compat::Semver{.parts = {2019}}}}),
        },
    });
}

TEST(BundlerLower, LowerExportStarAsNameCollision) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				import * as test from './nested'
				console.log(test.foo, test.oof)
				export * as ns from 'path1'
				let ns = 123
				export {ns as sn}
			
)pb"},
            {"/nested.js", R"pb(

				export * as foo from 'path2'
				let foo = 123
				export {foo as oof}
			
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
            .ExternalSettingsData = guchho::config::ExternalSettings{
                .PreResolve = guchho::config::ExternalMatchers{
                    .Exact = {{"path1", true}, {"path2", true}},
                },
            },
			.UnsupportedJSFeatures = guchho::compat::UnsupportedJSFeatures({{guchho::compat::Engine::kES, guchho::compat::Semver{.parts = {2019}}}}),
            
        },
    });
}

TEST(BundlerLower, LowerStrictModeSyntax) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				import './for-in'
			
)pb"},
            {"/for-in.js", R"pb(

				if (test)
					for (var a = b in {}) ;
				for (var x = y in {}) ;
			
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kESModule,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerLower, LowerForbidStrictModeSyntax) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				import './with'
				import './delete-1'
				import './delete-2'
				import './delete-3'
			
)pb"},
            {"/with.js", R"pb(

				with (x) y
			
)pb"},
            {"/delete-1.js", R"pb(

				delete x
			
)pb"},
            {"/delete-2.js", R"pb(

				delete (y)
			
)pb"},
            {"/delete-3.js", R"pb(

				delete (1 ? z : z)
			
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .OutputFormat = guchho::config::Format::kESModule,
            .AbsOutputFile = "/out.js",
        },
        .expected_scan_log = R"esl(
delete-1.js: ERROR: Delete of a bare identifier cannot be used with the "esm" output format due to strict mode
delete-2.js: ERROR: Delete of a bare identifier cannot be used with the "esm" output format due to strict mode
with.js: ERROR: With statements cannot be used with the "esm" output format due to strict mode
)esl",
    });
}

TEST(BundlerLower, LowerPrivateClassFieldOrder) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				class Foo {
					#foo = 123 // This must be set before "bar" is initialized
					bar = this.#foo
				}
				console.log(new Foo().bar === 123)
			
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kPassThrough,
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::JSFeature::kClassPrivateField,
        },
    });
}

TEST(BundlerLower, LowerPrivateClassMethodOrder) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				class Foo {
					bar = this.#foo()
					#foo() { return 123 } // This must be set before "bar" is initialized
				}
				console.log(new Foo().bar === 123)
			
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kPassThrough,
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::JSFeature::kClassPrivateMethod,
        },
    });
}

TEST(BundlerLower, LowerPrivateClassAccessorOrder) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				class Foo {
					bar = this.#foo
					get #foo() { return 123 } // This must be set before "bar" is initialized
				}
				console.log(new Foo().bar === 123)
			
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kPassThrough,
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::JSFeature::kClassPrivateAccessor,
        },
    });
}

TEST(BundlerLower, LowerPrivateClassStaticFieldOrder) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				class Foo {
					static #foo = 123 // This must be set before "bar" is initialized
					static bar = Foo.#foo
				}
				console.log(Foo.bar === 123)

				class FooThis {
					static #foo = 123 // This must be set before "bar" is initialized
					static bar = this.#foo
				}
				console.log(FooThis.bar === 123)
			
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kPassThrough,
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::JSFeature::kClassPrivateStaticField,
        },
    });
}

TEST(BundlerLower, LowerPrivateClassStaticMethodOrder) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				class Foo {
					static bar = Foo.#foo()
					static #foo() { return 123 } // This must be set before "bar" is initialized
				}
				console.log(Foo.bar === 123)

				class FooThis {
					static bar = this.#foo()
					static #foo() { return 123 } // This must be set before "bar" is initialized
				}
				console.log(FooThis.bar === 123)
			
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kPassThrough,
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::JSFeature::kClassPrivateStaticMethod,
        },
    });
}

TEST(BundlerLower, LowerPrivateClassStaticAccessorOrder) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				class Foo {
					static bar = Foo.#foo
					static get #foo() { return 123 } // This must be set before "bar" is initialized
				}
				console.log(Foo.bar === 123)

				class FooThis {
					static bar = this.#foo
					static get #foo() { return 123 } // This must be set before "bar" is initialized
				}
				console.log(FooThis.bar === 123)
			
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kPassThrough,
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::JSFeature::kClassPrivateStaticAccessor,
        },
    });
}

TEST(BundlerLower, LowerPrivateClassBrandCheckUnsupported) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				class Foo {
					#foo
					#bar
					baz() {
						return [
							this.#foo,
							this.#bar,
							#foo in this,
						]
					}
				}
			
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kPassThrough,
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::JSFeature::kClassPrivateBrandCheck,
        },
    });
}

TEST(BundlerLower, LowerPrivateClassBrandCheckSupported) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				class Foo {
					#foo
					#bar
					baz() {
						return [
							this.#foo,
							this.#bar,
							#foo in this,
						]
					}
				}
			
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kPassThrough,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerLower, LowerTemplateObject) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(
				x = () => [
					tag`x`,
					tag`\xFF`,
					tag`\x`,
					tag`\u`,
				]
				y = () => [
					tag`x${y}z`,
					tag`\xFF${y}z`,
					tag`x${y}\z`,
					tag`x${y}\u`,
				]
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kPassThrough,
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::JSFeature::kTemplateLiteral,
        },
    });
}

TEST(BundlerLower, LowerPrivateClassFieldStaticIssue1424) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				class T {
					#a() { return 'a'; }
					#b() { return 'b'; }
					static c;
					d() { console.log(this.#a()); }
				}
				new T().d();
			
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::JSFeature::kClassPrivateMethod,
        },
    });
}

TEST(BundlerLower, LowerNullishCoalescingAssignmentIssue1493) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				export class A {
					#a;
					f() {
						this.#a ??= 1;
					}
				}
			
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::JSFeature::kLogicalAssignment,
        },
    });
}

TEST(BundlerLower, StaticClassBlockESNext) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				class A {
					static {}
					static {
						this.thisField++
						A.classField++
						super.superField = super.superField + 1
						super.superField++
					}
				}
				let B = class {
					static {}
					static {
						this.thisField++
						super.superField = super.superField + 1
						super.superField++
					}
				}
			
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerLower, StaticClassBlockES2021) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				class A {
					static {}
					static {
						this.thisField++
						A.classField++
						super.superField = super.superField + 1
						super.superField++
					}
				}
				let B = class {
					static {}
					static {
						this.thisField++
						super.superField = super.superField + 1
						super.superField++
					}
				}
			
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::UnsupportedJSFeatures({{guchho::compat::Engine::kES, guchho::compat::Semver{.parts = {2021}}}}),
        },
    });
}

TEST(BundlerLower, LowerRegExpNameCollision) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				export function foo(RegExp) {
					return new RegExp(/./d, 'd')
				}
			
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::UnsupportedJSFeatures({{guchho::compat::Engine::kES, guchho::compat::Semver{.parts = {2021}}}}),
        },
    });
}

TEST(BundlerLower, LowerForAwait2017) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				export default [
					async () => { for await (x of y) z(x) },
					async () => { for await (x.y of y) z(x) },
					async () => { for await (let x of y) z(x) },
					async () => { for await (const x of y) z(x) },
					async () => { label: for await (const x of y) break label },
					async () => { label: for await (const x of y) continue label },
				]
			
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kPassThrough,
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::UnsupportedJSFeatures({{guchho::compat::Engine::kES, guchho::compat::Semver{.parts = {2017}}}}),
        },
    });
}

TEST(BundlerLower, LowerForAwait2015) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				export default [
					async () => { for await (x of y) z(x) },
					async () => { for await (x.y of y) z(x) },
					async () => { for await (let x of y) z(x) },
					async () => { for await (const x of y) z(x) },
					async () => { label: for await (const x of y) break label },
					async () => { label: for await (const x of y) continue label },
				]
			
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kPassThrough,
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::UnsupportedJSFeatures({{guchho::compat::Engine::kES, guchho::compat::Semver{.parts = {2015}}}}),
        },
    });
}

TEST(BundlerLower, LowerNestedFunctionDirectEval) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/1.js", R"pb(
if (foo) { function x() {} }
)pb"},
            {"/2.js", R"pb(
if (foo) { function x() {} eval('') }
)pb"},
            {"/3.js", R"pb(
if (foo) { function x() {} if (bar) { eval('') } }
)pb"},
            {"/4.js", R"pb(
if (foo) { eval(''); function x() {} }
)pb"},
            {"/5.js", R"pb(
'use strict'; if (foo) { function x() {} }
)pb"},
            {"/6.js", R"pb(
'use strict'; if (foo) { function x() {} eval('') }
)pb"},
            {"/7.js", R"pb(
'use strict'; if (foo) { function x() {} if (bar) { eval('') } }
)pb"},
            {"/8.js", R"pb(
'use strict'; if (foo) { eval(''); function x() {} }
)pb"},
        },
        .entry_paths = {"/1.js", "/2.js", "/3.js", "/4.js", "/5.js", "/6.js", "/7.js", "/8.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kPassThrough,
            .AbsOutputDir = "/out",
        },
    });
}

TEST(BundlerLower, JavaScriptDecoratorsESNext) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				@x.y()
				@(new y.x)
				export default class Foo {
					@x @y mUndef
					@x @y mDef = 1
					@x @y method() { return new Foo }
					@x @y static sUndef
					@x @y static sDef = new Foo
					@x @y static sMethod() { return new Foo }
				}
			
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kPassThrough,
            .AbsOutputFile = "/out.js",
        },
    });
}

TEST(BundlerLower, JavaScriptAutoAccessorESNext) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/js-define.js", R"pb(

				class Foo {
					accessor one = 1
					accessor #two = 2
					accessor [three()] = 3

					static accessor four = 4
					static accessor #five = 5
					static accessor [six()] = 6
				}
			
)pb"},
            {"/ts-define/ts-define.ts", R"pb(

				class Foo {
					accessor one = 1
					accessor #two = 2
					accessor [three()] = 3

					static accessor four = 4
					static accessor #five = 5
					static accessor [six()] = 6
				}
				class Normal { accessor a = b; c = d }
				class Private { accessor #a = b; c = d }
				class StaticNormal { static accessor a = b; static c = d }
				class StaticPrivate { static accessor #a = b; static c = d }
			
)pb"},
            {"/ts-define/tsconfig.json", R"pb(
{
				"compilerOptions": {
					"useDefineForClassFields": true,
				},
			}
)pb"},
            {"/ts-assign/ts-assign.ts", R"pb(

				class Foo {
					accessor one = 1
					accessor #two = 2
					accessor [three()] = 3

					static accessor four = 4
					static accessor #five = 5
					static accessor [six()] = 6
				}
				class Normal { accessor a = b; c = d }
				class Private { accessor #a = b; c = d }
				class StaticNormal { static accessor a = b; static c = d }
				class StaticPrivate { static accessor #a = b; static c = d }
			
)pb"},
            {"/ts-assign/tsconfig.json", R"pb(
{
				"compilerOptions": {
					"useDefineForClassFields": false,
				},
			}
)pb"},
        },
        .entry_paths = {"/js-define.js", "/ts-define/ts-define.ts", "/ts-assign/ts-assign.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kPassThrough,
            .AbsOutputDir = "/out",
        },
    });
}

TEST(BundlerLower, JavaScriptAutoAccessorES2022) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/js-define.js", R"pb(

				class Foo {
					accessor one = 1
					accessor #two = 2
					accessor [three()] = 3

					static accessor four = 4
					static accessor #five = 5
					static accessor [six()] = 6
				}
			
)pb"},
            {"/ts-define/ts-define.ts", R"pb(

				class Foo {
					accessor one = 1
					accessor #two = 2
					accessor [three()] = 3

					static accessor four = 4
					static accessor #five = 5
					static accessor [six()] = 6
				}
				class Normal { accessor a = b; c = d }
				class Private { accessor #a = b; c = d }
				class StaticNormal { static accessor a = b; static c = d }
				class StaticPrivate { static accessor #a = b; static c = d }
			
)pb"},
            {"/ts-define/tsconfig.json", R"pb(
{
				"compilerOptions": {
					"useDefineForClassFields": true,
				},
			}
)pb"},
            {"/ts-assign/ts-assign.ts", R"pb(

				class Foo {
					accessor one = 1
					accessor #two = 2
					accessor [three()] = 3

					static accessor four = 4
					static accessor #five = 5
					static accessor [six()] = 6
				}
				class Normal { accessor a = b; c = d }
				class Private { accessor #a = b; c = d }
				class StaticNormal { static accessor a = b; static c = d }
				class StaticPrivate { static accessor #a = b; static c = d }
			
)pb"},
            {"/ts-assign/tsconfig.json", R"pb(
{
				"compilerOptions": {
					"useDefineForClassFields": false,
				},
			}
)pb"},
        },
        .entry_paths = {"/js-define.js", "/ts-define/ts-define.ts", "/ts-assign/ts-assign.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kPassThrough,
            .AbsOutputDir = "/out",
            .UnsupportedJSFeatures = guchho::compat::UnsupportedJSFeatures({{guchho::compat::Engine::kES, guchho::compat::Semver{.parts = {2022}}}}),
        },
    });
}

TEST(BundlerLower, JavaScriptAutoAccessorES2021) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/js-define.js", R"pb(

				class Foo {
					accessor one = 1
					accessor #two = 2
					accessor [three()] = 3

					static accessor four = 4
					static accessor #five = 5
					static accessor [six()] = 6
				}
			
)pb"},
            {"/ts-define/ts-define.ts", R"pb(

				class Foo {
					accessor one = 1
					accessor #two = 2
					accessor [three()] = 3

					static accessor four = 4
					static accessor #five = 5
					static accessor [six()] = 6
				}
				class Normal { accessor a = b; c = d }
				class Private { accessor #a = b; c = d }
				class StaticNormal { static accessor a = b; static c = d }
				class StaticPrivate { static accessor #a = b; static c = d }
			
)pb"},
            {"/ts-define/tsconfig.json", R"pb(
{
				"compilerOptions": {
					"useDefineForClassFields": true,
				},
			}
)pb"},
            {"/ts-assign/ts-assign.ts", R"pb(

				class Foo {
					accessor one = 1
					accessor #two = 2
					accessor [three()] = 3

					static accessor four = 4
					static accessor #five = 5
					static accessor [six()] = 6
				}
				class Normal { accessor a = b; c = d }
				class Private { accessor #a = b; c = d }
				class StaticNormal { static accessor a = b; static c = d }
				class StaticPrivate { static accessor #a = b; static c = d }
			
)pb"},
            {"/ts-assign/tsconfig.json", R"pb(
{
				"compilerOptions": {
					"useDefineForClassFields": false,
				},
			}
)pb"},
        },
        .entry_paths = {"/js-define.js", "/ts-define/ts-define.ts", "/ts-assign/ts-assign.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kPassThrough,
            .AbsOutputDir = "/out",
            .UnsupportedJSFeatures = guchho::compat::UnsupportedJSFeatures({{guchho::compat::Engine::kES, guchho::compat::Semver{.parts = {2021}}}}),
        },
    });
}

TEST(BundlerLower, LowerUsing) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				using a = b
				await using c = d
				if (nested) {
					using x = 1
					await using y = 2
				}

				function foo() {
					using a = b
					if (nested) {
						using x = 1
					}
				}

				async function bar() {
					using a = b
					await using c = d
					if (nested) {
						using x = 1
						await using y = 2
					}
				}
			
)pb"},
            {"/loops.js", R"pb(

				for (using a of b) c(() => a)
				for (await using d of e) f(() => d)
				for await (using g of h) i(() => g)
				for await (await using j of k) l(() => j)

				if (nested) {
					for (using a of b) c(() => a)
					for (await using d of e) f(() => d)
					for await (using g of h) i(() => g)
					for await (await using j of k) l(() => j)
				}

				function foo() {
					for (using a of b) c(() => a)
				}

				async function bar() {
					for (using a of b) c(() => a)
					for (await using d of e) f(() => d)
					for await (using g of h) i(() => g)
					for await (await using j of k) l(() => j)
				}
			
)pb"},
        },
        .entry_paths = {"/entry.js", "/loops.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kPassThrough,
            .AbsOutputDir = "/out",
            .UnsupportedJSFeatures = guchho::compat::JSFeature::kUsing,
        },
    });
}

TEST(BundlerLower, LowerUsingUnsupportedAsync) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				function foo() {
					using a = b
					if (nested) {
						using x = 1
					}
				}

				async function bar() {
					using a = b
					await using c = d
					if (nested) {
						using x = 1
						await using y = 2
					}
				}
			
)pb"},
            {"/loops.js", R"pb(

				for (using a of b) c(() => a)

				if (nested) {
					for (using a of b) c(() => a)
				}

				function foo() {
					for (using a of b) c(() => a)
				}

				async function bar() {
					for (using a of b) c(() => a)
					for (await using d of e) f(() => d)
				}
			
)pb"},
        },
        .entry_paths = {"/entry.js", "/loops.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kPassThrough,
            .AbsOutputDir = "/out",
            .UnsupportedJSFeatures = guchho::compat::JSFeature::kAsyncAwait | guchho::compat::JSFeature::kTopLevelAwait,
        },
    });
}

TEST(BundlerLower, LowerUsingUnsupportedUsingAndAsync) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				function foo() {
					using a = b
					if (nested) {
						using x = 1
					}
				}

				async function bar() {
					using a = b
					await using c = d
					if (nested) {
						using x = 1
						await using y = 2
					}
				}
			
)pb"},
            {"/loops.js", R"pb(

				for (using a of b) c(() => a)

				if (nested) {
					for (using a of b) c(() => a)
				}

				function foo() {
					for (using a of b) c(() => a)
				}

				async function bar() {
					for (using a of b) c(() => a)
					for (await using d of e) f(() => d)
				}
			
)pb"},
        },
        .entry_paths = {"/entry.js", "/loops.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kPassThrough,
            .AbsOutputDir = "/out",
            .UnsupportedJSFeatures = guchho::compat::JSFeature::kUsing | guchho::compat::JSFeature::kAsyncAwait | guchho::compat::JSFeature::kTopLevelAwait,
        },
    });
}

TEST(BundlerLower, LowerUsingHoisting) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/hoist-use-strict.js", R"pb(

				"use strict"
				using a = b
				function foo() {
					"use strict"
					using a = b
				}
			
)pb"},
            {"/hoist-directive.js", R"pb(

				"use wtf"
				using a = b
				function foo() {
					"use wtf"
					using a = b
				}
			
)pb"},
            {"/hoist-import.js", R"pb(

				using a = b
				import "./foo"
				using c = d
			
)pb"},
            {"/hoist-export-star.js", R"pb(

				using a = b
				export * from './foo'
				using c = d
			
)pb"},
            {"/hoist-export-from.js", R"pb(

				using a = b
				export {x, y} from './foo'
				using c = d
			
)pb"},
            {"/hoist-export-clause.js", R"pb(

				using a = b
				export {a, c as 'c!'}
				using c = d
			
)pb"},
            {"/hoist-export-local-direct.js", R"pb(

				using a = b
				export var ac1 = [a, c], { x: [x1] } = foo
				export let a1 = a, { y: [y1] } = foo
				export const c1 = c, { z: [z1] } = foo
				var ac2 = [a, c], { x: [x2] } = foo
				let a2 = a, { y: [y2] } = foo
				const c2 = c, { z: [z2] } = foo
				using c = d
			
)pb"},
            {"/hoist-export-local-indirect.js", R"pb(

				using a = b
				var ac1 = [a, c], { x: [x1] } = foo
				let a1 = a, { y: [y1] } = foo
				const c1 = c, { z: [z1] } = foo
				var ac2 = [a, c], { x: [x2] } = foo
				let a2 = a, { y: [y2] } = foo
				const c2 = c, { z: [z2] } = foo
				using c = d
				export {x1, y1, z1}
			
)pb"},
            {"/hoist-export-class-direct.js", R"pb(

				using a = b
				export class Foo1 { ac = [a, c] }
				export class Bar1 { ac = [a, c, Bar1] }
				class Foo2 { ac = [a, c] }
				class Bar2 { ac = [a, c, Bar2] }
				using c = d
			
)pb"},
            {"/hoist-export-class-indirect.js", R"pb(

				using a = b
				class Foo1 { ac = [a, c] }
				class Bar1 { ac = [a, c, Bar1] }
				class Foo2 { ac = [a, c] }
				class Bar2 { ac = [a, c, Bar2] }
				using c = d
				export {Foo1, Bar1}
			
)pb"},
            {"/hoist-export-function-direct.js", R"pb(

				using a = b
				export function foo1() { return [a, c] }
				export function bar1() { return [a, c, bar1] }
				function foo2() { return [a, c] }
				function bar2() { return [a, c, bar2] }
				using c = d
			
)pb"},
            {"/hoist-export-function-indirect.js", R"pb(

				using a = b
				function foo1() { return [a, c] }
				function bar1() { return [a, c, bar1] }
				function foo2() { return [a, c] }
				function bar2() { return [a, c, bar2] }
				using c = d
				export {foo1, bar1}
			
)pb"},
            {"/hoist-export-default-class-name-unused.js", R"pb(

				using a = b
				export default class Foo {
					ac = [a, c]
				}
				using c = d
			
)pb"},
            {"/hoist-export-default-class-name-used.js", R"pb(

				using a = b
				export default class Foo {
					ac = [a, c, Foo]
				}
				using c = d
			
)pb"},
            {"/hoist-export-default-class-anonymous.js", R"pb(

				using a = b
				export default class {
					ac = [a, c]
				}
				using c = d
			
)pb"},
            {"/hoist-export-default-function-name-unused.js", R"pb(

				using a = b
				export default function foo() {
					return [a, c]
				}
				using c = d
			
)pb"},
            {"/hoist-export-default-function-name-used.js", R"pb(

				using a = b
				export default function foo() {
					return [a, c, foo]
				}
				using c = d
			
)pb"},
            {"/hoist-export-default-function-anonymous.js", R"pb(

				using a = b
				export default function() {
					return [a, c]
				}
				using c = d
			
)pb"},
            {"/hoist-export-default-expr.js", R"pb(

				using a = b
				export default [a, c]
				using c = d
			
)pb"},
        },
        .entry_paths = {"/hoist-use-strict.js", "/hoist-directive.js", "/hoist-import.js", "/hoist-export-star.js", "/hoist-export-from.js", "/hoist-export-clause.js", "/hoist-export-local-direct.js", "/hoist-export-local-indirect.js", "/hoist-export-class-direct.js", "/hoist-export-class-indirect.js", "/hoist-export-function-direct.js", "/hoist-export-function-indirect.js", "/hoist-export-default-class-name-unused.js", "/hoist-export-default-class-name-used.js", "/hoist-export-default-class-anonymous.js", "/hoist-export-default-function-name-unused.js", "/hoist-export-default-function-name-used.js", "/hoist-export-default-function-anonymous.js", "/hoist-export-default-expr.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kPassThrough,
            .AbsOutputDir = "/out",
            .UnsupportedJSFeatures = guchho::compat::JSFeature::kUsing,
        },
    });
}

TEST(BundlerLower, LowerUsingInsideTSNamespace) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"pb(

				namespace ns {
					export let a = b
					using c = d
					export let e = f
				}
			
)pb"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kPassThrough,
            .AbsOutputDir = "/out",
            .UnsupportedJSFeatures = guchho::compat::JSFeature::kUsing,
        },
    });
}

TEST(BundlerLower, LowerAsyncGenerator) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"pb(

				async function* foo() {
					yield
					yield x
					yield *x
					await using x = await y
					for await (let x of y) {}
					for await (await using x of y) {}
				}
				foo = async function* () {
					yield
					yield x
					yield *x
					await using x = await y
					for await (let x of y) {}
					for await (await using x of y) {}
				}
				foo = { async *bar () {
					yield
					yield x
					yield *x
					await using x = await y
					for await (let x of y) {}
					for await (await using x of y) {}
				} }
				class Foo { async *bar () {
					yield
					yield x
					yield *x
					await using x = await y
					for await (let x of y) {}
					for await (await using x of y) {}
				} }
				Foo = class { async *bar () {
					yield
					yield x
					yield *x
					await using x = await y
					for await (let x of y) {}
					for await (await using x of y) {}
				} }
				async function bar() {
					await using x = await y
					for await (let x of y) {}
					for await (await using x of y) {}
				}
			
)pb"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kPassThrough,
            .AbsOutputDir = "/out",
            .UnsupportedJSFeatures = guchho::compat::JSFeature::kAsyncGenerator,
        },
    });
}

TEST(BundlerLower, LowerAsyncGeneratorNoAwait) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"pb(

				async function* foo() {
					yield
					yield x
					yield *x
					await using x = await y
					for await (let x of y) {}
					for await (await using x of y) {}
				}
				foo = async function* () {
					yield
					yield x
					yield *x
					await using x = await y
					for await (let x of y) {}
					for await (await using x of y) {}
				}
				foo = { async *bar () {
					yield
					yield x
					yield *x
					await using x = await y
					for await (let x of y) {}
					for await (await using x of y) {}
				} }
				class Foo { async *bar () {
					yield
					yield x
					yield *x
					await using x = await y
					for await (let x of y) {}
					for await (await using x of y) {}
				} }
				Foo = class { async *bar () {
					yield
					yield x
					yield *x
					await using x = await y
					for await (let x of y) {}
					for await (await using x of y) {}
				} }
				async function bar() {
					await using x = await y
					for await (let x of y) {}
					for await (await using x of y) {}
				}
			
)pb"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kPassThrough,
            .AbsOutputDir = "/out",
            .UnsupportedJSFeatures = guchho::compat::JSFeature::kAsyncGenerator | guchho::compat::JSFeature::kAsyncAwait,
        },
    });
}

TEST(BundlerLower, JavaScriptDecoratorsBundleIssue3768) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/base-instance-method.js", R"pb(
class Foo { @dec foo() { return Foo } }
)pb"},
            {"/base-instance-field.js", R"pb(
class Foo { @dec foo = Foo }
)pb"},
            {"/base-instance-accessor.js", R"pb(
class Foo { @dec accessor foo = Foo }
)pb"},
            {"/base-static-method.js", R"pb(
class Foo { @dec static foo() { return Foo } }
)pb"},
            {"/base-static-field.js", R"pb(
class Foo { @dec static foo = Foo }
)pb"},
            {"/base-static-accessor.js", R"pb(
class Foo { @dec static accessor foo = Foo }
)pb"},
            {"/derived-instance-method.js", R"pb(
class Foo extends Bar { @dec foo() { return Foo } }
)pb"},
            {"/derived-instance-field.js", R"pb(
class Foo extends Bar { @dec foo = Foo }
)pb"},
            {"/derived-instance-accessor.js", R"pb(
class Foo extends Bar { @dec accessor foo = Foo }
)pb"},
            {"/derived-static-method.js", R"pb(
class Foo extends Bar { @dec static foo() { return Foo } }
)pb"},
            {"/derived-static-field.js", R"pb(
class Foo extends Bar { @dec static foo = Foo }
)pb"},
            {"/derived-static-accessor.js", R"pb(
class Foo extends Bar { @dec static accessor foo = Foo }
)pb"},
        },
        .entry_paths = {"/*"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputDir = "/out",
            .UnsupportedJSFeatures = guchho::compat::JSFeature::kDecorators,
        },
    });
}

TEST(BundlerLower, ForAwaitWithOptionalCatchIssue4378) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				async function test(b) {
					for await (const a of b) a()
				}
			
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kPassThrough,
            .AbsOutputFile = "/out.js",
            .MinifySyntax = true,
			.UnsupportedJSFeatures = guchho::compat::JSFeature::kForAwait,
        },
    });
}

TEST(BundlerLower, LowerConstIssue4448) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.ts", R"pb(

				import x = require('fs')
				console.log(import.meta.foo(x))
			
)pb"},
        },
        .entry_paths = {"/entry.ts"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kPassThrough,
            .AbsOutputFile = "/out.js",
            
			.UnsupportedJSFeatures = guchho::compat::JSFeature::kConstAndLet | guchho::compat::JSFeature::kImportMeta,
        },
        .expected_scan_log = R"esl(
entry.ts: WARNING: "import.meta" is not available in the configured target environment and will be empty
)esl",
    });
}

TEST(BundlerLower, LowerPrivateGetterSetter2015) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				export class Foo {
					get #foo() { return this.foo }
					set #bar(val) { this.bar = val }
					get #prop() { return this.prop }
					set #prop(val) { this.prop = val }
					foo(fn) {
						fn().#foo
						fn().#bar = 1
						fn().#prop
						fn().#prop = 2
					}
					unary(fn) {
						fn().#prop++;
						fn().#prop--;
						++fn().#prop;
						--fn().#prop;
					}
					binary(fn) {
						fn().#prop = 1;
						fn().#prop += 1;
						fn().#prop -= 1;
						fn().#prop *= 1;
						fn().#prop /= 1;
						fn().#prop %= 1;
						fn().#prop **= 1;
						fn().#prop <<= 1;
						fn().#prop >>= 1;
						fn().#prop >>>= 1;
						fn().#prop &= 1;
						fn().#prop |= 1;
						fn().#prop ^= 1;
						fn().#prop &&= 1;
						fn().#prop ||= 1;
						fn().#prop ??= 1;
					}
				}
			
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .BuildMode = guchho::config::Mode::kBundle,
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::UnsupportedJSFeatures({{guchho::compat::Engine::kES, guchho::compat::Semver{.parts = {2015}}}}),
        },
    });
}

TEST(BundlerLower, LowerAsyncSuperES2016NoBundle) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				class Derived extends Base {
					async test(key) {
						return [
							await super.foo,
							await super[key],
							await ([super.foo] = [0]),
							await ([super[key]] = [0]),

							await (super.foo = 1),
							await (super[key] = 1),
							await (super.foo += 2),
							await (super[key] += 2),

							await ++super.foo,
							await ++super[key],
							await super.foo++,
							await super[key]++,

							await super.foo.name,
							await super[key].name,
							await super.foo?.name,
							await super[key]?.name,

							await super.foo(1, 2),
							await super[key](1, 2),
							await super.foo?.(1, 2),
							await super[key]?.(1, 2),

							await (() => super.foo)(),
							await (() => super[key])(),
							await (() => super.foo())(),
							await (() => super[key]())(),

							await super.foo``,
							await super[key]``,
						]
					}
				}

				// This covers a bug that caused a compiler crash
				let fn = async () => class extends Base {
					a = super.a
					b = () => super.b
					c() { return super.c }
					d() { return () => super.d }
				}

				// This covers a bug that generated bad code
				class Derived2 extends Base {
					async a() { return class { [super.foo] = 123 } }
					b = async () => class { [super.foo] = 123 }
				}

				// This covers putting the generated temporary variable inside the loop
				for (let i = 0; i < 3; i++) {
					objs.push({
						__proto__: {
							foo() { return i },
						},
						async bar() { return super.foo() },
					})
				}
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::UnsupportedJSFeatures({{guchho::compat::Engine::kES, guchho::compat::Semver{.parts = {2016}}}}),
        },
    });
}

TEST(BundlerLower, LowerStaticAsyncSuperES2021NoBundle) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				class Derived extends Base {
					static test = async (key) => {
						return [
							await super.foo,
							await super[key],
							await ([super.foo] = [0]),
							await ([super[key]] = [0]),

							await (super.foo = 1),
							await (super[key] = 1),
							await (super.foo += 2),
							await (super[key] += 2),

							await ++super.foo,
							await ++super[key],
							await super.foo++,
							await super[key]++,

							await super.foo.name,
							await super[key].name,
							await super.foo?.name,
							await super[key]?.name,

							await super.foo(1, 2),
							await super[key](1, 2),
							await super.foo?.(1, 2),
							await super[key]?.(1, 2),

							await (() => super.foo)(),
							await (() => super[key])(),
							await (() => super.foo())(),
							await (() => super[key]())(),

							await super.foo``,
							await super[key]``,
						]
					}
				}

				// This covers a bug that caused a compiler crash
				let fn = async () => class extends Base {
					static a = super.a
					static b = () => super.b
					static c() { return super.c }
					static d() { return () => super.d }
				}

				// This covers a bug that generated bad code
				class Derived2 extends Base {
					static async a() { return class { [super.foo] = 123 } }
					static b = async () => class { [super.foo] = 123 }
				}
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::UnsupportedJSFeatures({{guchho::compat::Engine::kES, guchho::compat::Semver{.parts = {2021}}}}),
        },
    });
}

TEST(BundlerLower, LowerStaticAsyncSuperES2016NoBundle) {
    lower_suite.ExpectBundled(Bundled{
        .files = {
            {"/entry.js", R"pb(

				class Derived extends Base {
					static test = async (key) => {
						return [
							await super.foo,
							await super[key],
							await ([super.foo] = [0]),
							await ([super[key]] = [0]),

							await (super.foo = 1),
							await (super[key] = 1),
							await (super.foo += 2),
							await (super[key] += 2),

							await ++super.foo,
							await ++super[key],
							await super.foo++,
							await super[key]++,

							await super.foo.name,
							await super[key].name,
							await super.foo?.name,
							await super[key]?.name,

							await super.foo(1, 2),
							await super[key](1, 2),
							await super.foo?.(1, 2),
							await super[key]?.(1, 2),

							await (() => super.foo)(),
							await (() => super[key])(),
							await (() => super.foo())(),
							await (() => super[key]())(),

							await super.foo``,
							await super[key]``,
						]
					}
				}

				// This covers a bug that caused a compiler crash
				let fn = async () => class extends Base {
					static a = super.a
					static b = () => super.b
					static c() { return super.c }
					static d() { return () => super.d }
				}

				// This covers a bug that generated bad code
				class Derived2 extends Base {
					static async a() { return class { [super.foo] = 123 } }
					static b = async () => class { [super.foo] = 123 }
				}
)pb"},
        },
        .entry_paths = {"/entry.js"},
        .options = guchho::config::Options{
            .AbsOutputFile = "/out.js",
            .UnsupportedJSFeatures = guchho::compat::UnsupportedJSFeatures({{guchho::compat::Engine::kES, guchho::compat::Semver{.parts = {2016}}}}),
        },
    });
}

} // namespace bundler::test

#include "guchho/config.hpp"
#include "guchho/helpers.hpp"

namespace guchho::config {

    namespace {
        
        // GetKnownGlobals
        // ---------------
        // Returns the complete list of JavaScript global identifiers and
        // property paths that Guchho considers "known" during tree-shaking
        // and dead-code elimination.  Each entry is a vector of strings
        // representing a dotted path:
        //
        //   Single-element   A bare global (e.g. {"Math"}).
        //   Multi-element    A property chain (e.g. {"Math", "PI"} or
        //                    {"Object", "prototype", "toString"}).
        //
        // The list covers:
        //   - ECMAScript built-ins (Array, Object, Math, Reflect, JSON, etc.)
        //   - TypedArray static methods (from, of, fromBase64, fromHex)
        //   - Symbol well-known symbols (iterator, asyncIterator, dispose, etc.)
        //   - Browser DOM constructors and interfaces (HTML*, SVG*, WebGL*, etc.)
        //   - Browser globals (window, document, console, fetch, etc.)
        //   - Window event handler properties (onclick, onkeydown, etc.)
        //   - Console methods (log, error, warn, table, etc.)
        //   - CSSOM constructors (CSSStyleRule, CSSStyleSheet, etc.)
        //
        // A reference to any of these paths is considered side-effect-free
        // and may be removed if the reference is unused.
        //
        // Input:  (none — static data)
        // Output: reference to the static global list
        static const std::vector<std::vector<std::string>>& GetKnownGlobals() {
            static const std::vector<std::vector<std::string>> kGlobals = {
                // Array: Static methods
                {"Array", "from"},
                {"Array", "fromAsync"},
                {"Array", "isArray"},
                {"Array", "of"},

                // RegExp: Static methods
                {"RegExp", "escape"},

                // Map: Static methods
                {"Map", "groupBy"},

                // Object: Static methods
                {"Object", "assign"},
                {"Object", "create"},
                {"Object", "defineProperties"},
                {"Object", "defineProperty"},
                {"Object", "entries"},
                {"Object", "freeze"},
                {"Object", "fromEntries"},
                {"Object", "getOwnPropertyDescriptor"},
                {"Object", "getOwnPropertyDescriptors"},
                {"Object", "getOwnPropertyNames"},
                {"Object", "getOwnPropertySymbols"},
                {"Object", "getPrototypeOf"},
                {"Object", "is"},
                {"Object", "isExtensible"},
                {"Object", "isFrozen"},
                {"Object", "isSealed"},
                {"Object", "keys"},
                {"Object", "preventExtensions"},
                {"Object", "seal"},
                {"Object", "setPrototypeOf"},
                {"Object", "values"},

                // Object: Instance methods
                {"Object", "prototype", "__defineGetter__"},
                {"Object", "prototype", "__defineSetter__"},
                {"Object", "prototype", "__lookupGetter__"},
                {"Object", "prototype", "__lookupSetter__"},
                {"Object", "prototype", "hasOwnProperty"},
                {"Object", "prototype", "isPrototypeOf"},
                {"Object", "prototype", "propertyIsEnumerable"},
                {"Object", "prototype", "toLocaleString"},
                {"Object", "prototype", "toString"},
                {"Object", "prototype", "unwatch"},
                {"Object", "prototype", "valueOf"},
                {"Object", "prototype", "watch"},

                // Symbol: Static properties (well-known symbols)
                {"Symbol", "asyncDispose"},
                {"Symbol", "asyncIterator"},
                {"Symbol", "dispose"},
                {"Symbol", "hasInstance"},
                {"Symbol", "isConcatSpreadable"},
                {"Symbol", "iterator"},
                {"Symbol", "match"},
                {"Symbol", "matchAll"},
                {"Symbol", "replace"},
                {"Symbol", "search"},
                {"Symbol", "species"},
                {"Symbol", "split"},
                {"Symbol", "toPrimitive"},
                {"Symbol", "toStringTag"},
                {"Symbol", "unscopables"},

                // Math: Static properties
                {"Math", "E"},
                {"Math", "LN10"},
                {"Math", "LN2"},
                {"Math", "LOG10E"},
                {"Math", "LOG2E"},
                {"Math", "PI"},
                {"Math", "SQRT1_2"},
                {"Math", "SQRT2"},

                // Math: Static methods
                {"Math", "abs"},
                {"Math", "acos"},
                {"Math", "acosh"},
                {"Math", "asin"},
                {"Math", "asinh"},
                {"Math", "atan"},
                {"Math", "atan2"},
                {"Math", "atanh"},
                {"Math", "cbrt"},
                {"Math", "ceil"},
                {"Math", "clz32"},
                {"Math", "cos"},
                {"Math", "cosh"},
                {"Math", "exp"},
                {"Math", "expm1"},
                {"Math", "floor"},
                {"Math", "fround"},
                {"Math", "hypot"},
                {"Math", "imul"},
                {"Math", "log"},
                {"Math", "log10"},
                {"Math", "log1p"},
                {"Math", "log2"},
                {"Math", "max"},
                {"Math", "min"},
                {"Math", "pow"},
                {"Math", "random"},
                {"Math", "round"},
                {"Math", "sign"},
                {"Math", "sin"},
                {"Math", "sinh"},
                {"Math", "sqrt"},
                {"Math", "tan"},
                {"Math", "tanh"},
                {"Math", "trunc"},

                // Reflect: Static methods
                {"Reflect", "apply"},
                {"Reflect", "construct"},
                {"Reflect", "defineProperty"},
                {"Reflect", "deleteProperty"},
                {"Reflect", "get"},
                {"Reflect", "getOwnPropertyDescriptor"},
                {"Reflect", "getPrototypeOf"},
                {"Reflect", "has"},
                {"Reflect", "isExtensible"},
                {"Reflect", "ownKeys"},
                {"Reflect", "preventExtensions"},
                {"Reflect", "set"},
                {"Reflect", "setPrototypeOf"},

                // JSON: Static methods
                {"JSON", "parse"},
                {"JSON", "stringify"},

                // TypedArray: Static methods (from, of, fromBase64, fromHex)
                {"BigInt64Array", "from"},
                {"BigInt64Array", "of"},
                {"BigUint64Array", "from"},
                {"BigUint64Array", "of"},
                {"Float16Array", "from"},
                {"Float16Array", "of"},
                {"Float32Array", "from"},
                {"Float32Array", "of"},
                {"Float64Array", "from"},
                {"Float64Array", "of"},
                {"Int16Array", "from"},
                {"Int16Array", "of"},
                {"Int32Array", "from"},
                {"Int32Array", "of"},
                {"Int8Array", "from"},
                {"Int8Array", "of"},
                {"Uint16Array", "from"},
                {"Uint16Array", "of"},
                {"Uint32Array", "from"},
                {"Uint32Array", "of"},
                {"Uint8Array", "from"},
                {"Uint8Array", "fromBase64"},
                {"Uint8Array", "fromHex"},
                {"Uint8Array", "of"},
                {"Uint8ClampedArray", "from"},
                {"Uint8ClampedArray", "of"},

                // Other globals present in both the browser and node
                {"AbortController"},
                {"AbortSignal"},
                {"AggregateError"},
                {"Array"},
                {"ArrayBuffer"},
                {"Atomics"},
                {"BigInt"},
                {"BigInt64Array"},
                {"BigUint64Array"},
                {"Boolean"},
                {"DataView"},
                {"Date"},
                {"Error"},
                {"EvalError"},
                {"Event"},
                {"EventTarget"},
                {"FinalizationRegistry"},
                {"Float16Array"},
                {"Float32Array"},
                {"Float64Array"},
                {"Function"},
                {"Int16Array"},
                {"Int32Array"},
                {"Int8Array"},
                {"Intl"},
                {"Iterator"},
                {"JSON"},
                {"Map"},
                {"Math"},
                {"MessageChannel"},
                {"MessageEvent"},
                {"MessagePort"},
                {"Number"},
                {"Object"},
                {"Promise"},
                {"Proxy"},
                {"RangeError"},
                {"ReferenceError"},
                {"Reflect"},
                {"RegExp"},
                {"Set"},
                {"SharedArrayBuffer"},
                {"String"},
                {"Symbol"},
                {"SyntaxError"},
                {"TextDecoder"},
                {"TextEncoder"},
                {"TypeError"},
                {"URIError"},
                {"URL"},
                {"URLSearchParams"},
                {"Uint16Array"},
                {"Uint32Array"},
                {"Uint8Array"},
                {"Uint8ClampedArray"},
                {"WeakMap"},
                {"WeakRef"},
                {"WeakSet"},
                {"WebAssembly"},
                {"clearInterval"},
                {"clearTimeout"},
                {"console"},
                {"decodeURI"},
                {"decodeURIComponent"},
                {"encodeURI"},
                {"encodeURIComponent"},
                {"escape"},
                {"globalThis"},
                {"isFinite"},
                {"isNaN"},
                {"parseFloat"},
                {"parseInt"},
                {"queueMicrotask"},
                {"setInterval"},
                {"setTimeout"},
                {"unescape"},

                // Console method references
                {"console", "assert"},
                {"console", "clear"},
                {"console", "count"},
                {"console", "countReset"},
                {"console", "debug"},
                {"console", "dir"},
                {"console", "dirxml"},
                {"console", "error"},
                {"console", "group"},
                {"console", "groupCollapsed"},
                {"console", "groupEnd"},
                {"console", "info"},
                {"console", "log"},
                {"console", "table"},
                {"console", "time"},
                {"console", "timeEnd"},
                {"console", "timeLog"},
                {"console", "trace"},
                {"console", "warn"},

                // CSSOM APIs
                {"CSSAnimation"},
                {"CSSFontFaceRule"},
                {"CSSImportRule"},
                {"CSSKeyframeRule"},
                {"CSSKeyframesRule"},
                {"CSSMediaRule"},
                {"CSSNamespaceRule"},
                {"CSSPageRule"},
                {"CSSRule"},
                {"CSSRuleList"},
                {"CSSStyleDeclaration"},
                {"CSSStyleRule"},
                {"CSSStyleSheet"},
                {"CSSSupportsRule"},
                {"CSSTransition"},

                // SVG DOM
                {"SVGAElement"},
                {"SVGAngle"},
                {"SVGAnimateElement"},
                {"SVGAnimateMotionElement"},
                {"SVGAnimateTransformElement"},
                {"SVGAnimatedAngle"},
                {"SVGAnimatedBoolean"},
                {"SVGAnimatedEnumeration"},
                {"SVGAnimatedInteger"},
                {"SVGAnimatedLength"},
                {"SVGAnimatedLengthList"},
                {"SVGAnimatedNumber"},
                {"SVGAnimatedNumberList"},
                {"SVGAnimatedPreserveAspectRatio"},
                {"SVGAnimatedRect"},
                {"SVGAnimatedString"},
                {"SVGAnimatedTransformList"},
                {"SVGAnimationElement"},
                {"SVGCircleElement"},
                {"SVGClipPathElement"},
                {"SVGComponentTransferFunctionElement"},
                {"SVGDefsElement"},
                {"SVGDescElement"},
                {"SVGElement"},
                {"SVGEllipseElement"},
                {"SVGFEBlendElement"},
                {"SVGFEColorMatrixElement"},
                {"SVGFEComponentTransferElement"},
                {"SVGFECompositeElement"},
                {"SVGFEConvolveMatrixElement"},
                {"SVGFEDiffuseLightingElement"},
                {"SVGFEDisplacementMapElement"},
                {"SVGFEDistantLightElement"},
                {"SVGFEDropShadowElement"},
                {"SVGFEFloodElement"},
                {"SVGFEFuncAElement"},
                {"SVGFEFuncBElement"},
                {"SVGFEFuncGElement"},
                {"SVGFEFuncRElement"},
                {"SVGFEGaussianBlurElement"},
                {"SVGFEImageElement"},
                {"SVGFEMergeElement"},
                {"SVGFEMergeNodeElement"},
                {"SVGFEMorphologyElement"},
                {"SVGFEOffsetElement"},
                {"SVGFEPointLightElement"},
                {"SVGFESpecularLightingElement"},
                {"SVGFESpotLightElement"},
                {"SVGFETileElement"},
                {"SVGFETurbulenceElement"},
                {"SVGFilterElement"},
                {"SVGForeignObjectElement"},
                {"SVGGElement"},
                {"SVGGeometryElement"},
                {"SVGGradientElement"},
                {"SVGGraphicsElement"},
                {"SVGImageElement"},
                {"SVGLength"},
                {"SVGLengthList"},
                {"SVGLineElement"},
                {"SVGLinearGradientElement"},
                {"SVGMPathElement"},
                {"SVGMarkerElement"},
                {"SVGMaskElement"},
                {"SVGMatrix"},
                {"SVGMetadataElement"},
                {"SVGNumber"},
                {"SVGNumberList"},
                {"SVGPathElement"},
                {"SVGPatternElement"},
                {"SVGPoint"},
                {"SVGPointList"},
                {"SVGPolygonElement"},
                {"SVGPolylineElement"},
                {"SVGPreserveAspectRatio"},
                {"SVGRadialGradientElement"},
                {"SVGRect"},
                {"SVGRectElement"},
                {"SVGSVGElement"},
                {"SVGScriptElement"},
                {"SVGSetElement"},
                {"SVGStopElement"},
                {"SVGStringList"},
                {"SVGStyleElement"},
                {"SVGSwitchElement"},
                {"SVGSymbolElement"},
                {"SVGTSpanElement"},
                {"SVGTextContentElement"},
                {"SVGTextElement"},
                {"SVGTextPathElement"},
                {"SVGTextPositioningElement"},
                {"SVGTitleElement"},
                {"SVGTransform"},
                {"SVGTransformList"},
                {"SVGUnitTypes"},
                {"SVGUseElement"},
                {"SVGViewElement"},

                // Other browser APIs
                {"AnalyserNode"},
                {"Animation"},
                {"AnimationEffect"},
                {"AnimationEvent"},
                {"AnimationPlaybackEvent"},
                {"AnimationTimeline"},
                {"Attr"},
                {"Audio"},
                {"AudioBuffer"},
                {"AudioBufferSourceNode"},
                {"AudioDestinationNode"},
                {"AudioListener"},
                {"AudioNode"},
                {"AudioParam"},
                {"AudioProcessingEvent"},
                {"AudioScheduledSourceNode"},
                {"BarProp"},
                {"BeforeUnloadEvent"},
                {"BiquadFilterNode"},
                {"Blob"},
                {"BlobEvent"},
                {"ByteLengthQueuingStrategy"},
                {"CDATASection"},
                {"CSS"},
                {"CanvasGradient"},
                {"CanvasPattern"},
                {"CanvasRenderingContext2D"},
                {"ChannelMergerNode"},
                {"ChannelSplitterNode"},
                {"CharacterData"},
                {"ClipboardEvent"},
                {"CloseEvent"},
                {"Comment"},
                {"CompositionEvent"},
                {"ConvolverNode"},
                {"CountQueuingStrategy"},
                {"Crypto"},
                {"CustomElementRegistry"},
                {"CustomEvent"},
                {"DOMException"},
                {"DOMImplementation"},
                {"DOMMatrix"},
                {"DOMMatrixReadOnly"},
                {"DOMParser"},
                {"DOMPoint"},
                {"DOMPointReadOnly"},
                {"DOMQuad"},
                {"DOMRect"},
                {"DOMRectList"},
                {"DOMRectReadOnly"},
                {"DOMStringList"},
                {"DOMStringMap"},
                {"DOMTokenList"},
                {"DataTransfer"},
                {"DataTransferItem"},
                {"DataTransferItemList"},
                {"DelayNode"},
                {"Document"},
                {"DocumentFragment"},
                {"DocumentTimeline"},
                {"DocumentType"},
                {"DragEvent"},
                {"DynamicsCompressorNode"},
                {"Element"},
                {"ErrorEvent"},
                {"EventSource"},
                {"File"},
                {"FileList"},
                {"FileReader"},
                {"FocusEvent"},
                {"FontFace"},
                {"FormData"},
                {"GainNode"},
                {"Gamepad"},
                {"GamepadButton"},
                {"GamepadEvent"},
                {"Geolocation"},
                {"GeolocationPositionError"},
                {"HTMLAllCollection"},
                {"HTMLAnchorElement"},
                {"HTMLAreaElement"},
                {"HTMLAudioElement"},
                {"HTMLBRElement"},
                {"HTMLBaseElement"},
                {"HTMLBodyElement"},
                {"HTMLButtonElement"},
                {"HTMLCanvasElement"},
                {"HTMLCollection"},
                {"HTMLDListElement"},
                {"HTMLDataElement"},
                {"HTMLDataListElement"},
                {"HTMLDetailsElement"},
                {"HTMLDirectoryElement"},
                {"HTMLDivElement"},
                {"HTMLDocument"},
                {"HTMLElement"},
                {"HTMLEmbedElement"},
                {"HTMLFieldSetElement"},
                {"HTMLFontElement"},
                {"HTMLFormControlsCollection"},
                {"HTMLFormElement"},
                {"HTMLFrameElement"},
                {"HTMLFrameSetElement"},
                {"HTMLHRElement"},
                {"HTMLHeadElement"},
                {"HTMLHeadingElement"},
                {"HTMLHtmlElement"},
                {"HTMLIFrameElement"},
                {"HTMLImageElement"},
                {"HTMLInputElement"},
                {"HTMLLIElement"},
                {"HTMLLabelElement"},
                {"HTMLLegendElement"},
                {"HTMLLinkElement"},
                {"HTMLMapElement"},
                {"HTMLMarqueeElement"},
                {"HTMLMediaElement"},
                {"HTMLMenuElement"},
                {"HTMLMetaElement"},
                {"HTMLMeterElement"},
                {"HTMLModElement"},
                {"HTMLOListElement"},
                {"HTMLObjectElement"},
                {"HTMLOptGroupElement"},
                {"HTMLOptionElement"},
                {"HTMLOptionsCollection"},
                {"HTMLOutputElement"},
                {"HTMLParagraphElement"},
                {"HTMLParamElement"},
                {"HTMLPictureElement"},
                {"HTMLPreElement"},
                {"HTMLProgressElement"},
                {"HTMLQuoteElement"},
                {"HTMLScriptElement"},
                {"HTMLSelectElement"},
                {"HTMLSlotElement"},
                {"HTMLSourceElement"},
                {"HTMLSpanElement"},
                {"HTMLStyleElement"},
                {"HTMLTableCaptionElement"},
                {"HTMLTableCellElement"},
                {"HTMLTableColElement"},
                {"HTMLTableElement"},
                {"HTMLTableRowElement"},
                {"HTMLTableSectionElement"},
                {"HTMLTemplateElement"},
                {"HTMLTextAreaElement"},
                {"HTMLTimeElement"},
                {"HTMLTitleElement"},
                {"HTMLTrackElement"},
                {"HTMLUListElement"},
                {"HTMLUnknownElement"},
                {"HTMLVideoElement"},
                {"HashChangeEvent"},
                {"Headers"},
                {"History"},
                {"IDBCursor"},
                {"IDBCursorWithValue"},
                {"IDBDatabase"},
                {"IDBFactory"},
                {"IDBIndex"},
                {"IDBKeyRange"},
                {"IDBObjectStore"},
                {"IDBOpenDBRequest"},
                {"IDBRequest"},
                {"IDBTransaction"},
                {"IDBVersionChangeEvent"},
                {"Image"},
                {"ImageData"},
                {"InputEvent"},
                {"IntersectionObserver"},
                {"IntersectionObserverEntry"},
                {"KeyboardEvent"},
                {"KeyframeEffect"},
                {"Location"},
                {"MediaCapabilities"},
                {"MediaElementAudioSourceNode"},
                {"MediaEncryptedEvent"},
                {"MediaError"},
                {"MediaList"},
                {"MediaQueryList"},
                {"MediaQueryListEvent"},
                {"MediaRecorder"},
                {"MediaSource"},
                {"MediaStream"},
                {"MediaStreamAudioDestinationNode"},
                {"MediaStreamAudioSourceNode"},
                {"MediaStreamTrack"},
                {"MediaStreamTrackEvent"},
                {"MimeType"},
                {"MimeTypeArray"},
                {"MouseEvent"},
                {"MutationEvent"},
                {"MutationObserver"},
                {"MutationRecord"},
                {"NamedNodeMap"},
                {"Navigator"},
                {"Node"},
                {"NodeFilter"},
                {"NodeIterator"},
                {"NodeList"},
                {"Notification"},
                {"OfflineAudioCompletionEvent"},
                {"Option"},
                {"OscillatorNode"},
                {"PageTransitionEvent"},
                {"Path2D"},
                {"Performance"},
                {"PerformanceEntry"},
                {"PerformanceMark"},
                {"PerformanceMeasure"},
                {"PerformanceNavigation"},
                {"PerformanceObserver"},
                {"PerformanceObserverEntryList"},
                {"PerformanceResourceTiming"},
                {"PerformanceTiming"},
                {"PeriodicWave"},
                {"Plugin"},
                {"PluginArray"},
                {"PointerEvent"},
                {"PopStateEvent"},
                {"ProcessingInstruction"},
                {"ProgressEvent"},
                {"PromiseRejectionEvent"},
                {"RTCCertificate"},
                {"RTCDTMFSender"},
                {"RTCDTMFToneChangeEvent"},
                {"RTCDataChannel"},
                {"RTCDataChannelEvent"},
                {"RTCIceCandidate"},
                {"RTCPeerConnection"},
                {"RTCPeerConnectionIceEvent"},
                {"RTCRtpReceiver"},
                {"RTCRtpSender"},
                {"RTCRtpTransceiver"},
                {"RTCSessionDescription"},
                {"RTCStatsReport"},
                {"RTCTrackEvent"},
                {"RadioNodeList"},
                {"Range"},
                {"ReadableStream"},
                {"Request"},
                {"ResizeObserver"},
                {"ResizeObserverEntry"},
                {"Response"},
                {"Screen"},
                {"ScriptProcessorNode"},
                {"SecurityPolicyViolationEvent"},
                {"Selection"},
                {"ShadowRoot"},
                {"SourceBuffer"},
                {"SourceBufferList"},
                {"SpeechSynthesisEvent"},
                {"SpeechSynthesisUtterance"},
                {"StaticRange"},
                {"Storage"},
                {"StorageEvent"},
                {"StyleSheet"},
                {"StyleSheetList"},
                {"Text"},
                {"TextMetrics"},
                {"TextTrack"},
                {"TextTrackCue"},
                {"TextTrackCueList"},
                {"TextTrackList"},
                {"TimeRanges"},
                {"TrackEvent"},
                {"TransitionEvent"},
                {"TreeWalker"},
                {"UIEvent"},
                {"VTTCue"},
                {"ValidityState"},
                {"VisualViewport"},
                {"WaveShaperNode"},
                {"WebGLActiveInfo"},
                {"WebGLBuffer"},
                {"WebGLContextEvent"},
                {"WebGLFramebuffer"},
                {"WebGLProgram"},
                {"WebGLQuery"},
                {"WebGLRenderbuffer"},
                {"WebGLRenderingContext"},
                {"WebGLSampler"},
                {"WebGLShader"},
                {"WebGLShaderPrecisionFormat"},
                {"WebGLSync"},
                {"WebGLTexture"},
                {"WebGLUniformLocation"},
                {"WebKitCSSMatrix"},
                {"WebSocket"},
                {"WheelEvent"},
                {"Window"},
                {"Worker"},
                {"XMLDocument"},
                {"XMLHttpRequest"},
                {"XMLHttpRequestEventTarget"},
                {"XMLHttpRequestUpload"},
                {"XMLSerializer"},
                {"XPathEvaluator"},
                {"XPathExpression"},
                {"XPathResult"},
                {"XSLTProcessor"},
                {"alert"},
                {"atob"},
                {"blur"},
                {"btoa"},
                {"cancelAnimationFrame"},
                {"captureEvents"},
                {"close"},
                {"closed"},
                {"confirm"},
                {"customElements"},
                {"devicePixelRatio"},
                {"document"},
                {"event"},
                {"fetch"},
                {"find"},
                {"focus"},
                {"frameElement"},
                {"frames"},
                {"getComputedStyle"},
                {"getSelection"},
                {"history"},
                {"indexedDB"},
                {"isSecureContext"},
                {"length"},
                {"location"},
                {"locationbar"},
                {"matchMedia"},
                {"menubar"},
                {"moveBy"},
                {"moveTo"},
                {"name"},
                {"navigator"},
                {"onabort"},
                {"onafterprint"},
                {"onanimationend"},
                {"onanimationiteration"},
                {"onanimationstart"},
                {"onbeforeprint"},
                {"onbeforeunload"},
                {"onblur"},
                {"oncanplay"},
                {"oncanplaythrough"},
                {"onchange"},
                {"onclick"},
                {"oncontextmenu"},
                {"oncuechange"},
                {"ondblclick"},
                {"ondrag"},
                {"ondragend"},
                {"ondragenter"},
                {"ondragleave"},
                {"ondragover"},
                {"ondragstart"},
                {"ondrop"},
                {"ondurationchange"},
                {"onemptied"},
                {"onended"},
                {"onerror"},
                {"onfocus"},
                {"ongotpointercapture"},
                {"onhashchange"},
                {"oninput"},
                {"oninvalid"},
                {"onkeydown"},
                {"onkeypress"},
                {"onkeyup"},
                {"onlanguagechange"},
                {"onload"},
                {"onloadeddata"},
                {"onloadedmetadata"},
                {"onloadstart"},
                {"onlostpointercapture"},
                {"onmessage"},
                {"onmousedown"},
                {"onmouseenter"},
                {"onmouseleave"},
                {"onmousemove"},
                {"onmouseout"},
                {"onmouseover"},
                {"onmouseup"},
                {"onoffline"},
                {"ononline"},
                {"onpagehide"},
                {"onpageshow"},
                {"onpause"},
                {"onplay"},
                {"onplaying"},
                {"onpointercancel"},
                {"onpointerdown"},
                {"onpointerenter"},
                {"onpointerleave"},
                {"onpointermove"},
                {"onpointerout"},
                {"onpointerover"},
                {"onpointerup"},
                {"onpopstate"},
                {"onprogress"},
                {"onratechange"},
                {"onrejectionhandled"},
                {"onreset"},
                {"onresize"},
                {"onscroll"},
                {"onseeked"},
                {"onseeking"},
                {"onselect"},
                {"onstalled"},
                {"onstorage"},
                {"onsubmit"},
                {"onsuspend"},
                {"ontimeupdate"},
                {"ontoggle"},
                {"ontransitioncancel"},
                {"ontransitionend"},
                {"ontransitionrun"},
                {"ontransitionstart"},
                {"onunhandledrejection"},
                {"onunload"},
                {"onvolumechange"},
                {"onwaiting"},
                {"onwebkitanimationend"},
                {"onwebkitanimationiteration"},
                {"onwebkitanimationstart"},
                {"onwebkittransitionend"},
                {"onwheel"},
                {"open"},
                {"opener"},
                {"origin"},
                {"outerHeight"},
                {"outerWidth"},
                {"parent"},
                {"performance"},
                {"personalbar"},
                {"postMessage"},
                {"print"},
                {"prompt"},
                {"releaseEvents"},
                {"requestAnimationFrame"},
                {"resizeBy"},
                {"resizeTo"},
                {"screen"},
                {"screenLeft"},
                {"screenTop"},
                {"screenX"},
                {"screenY"},
                {"scroll"},
                {"scrollBy"},
                {"scrollTo"},
                {"scrollbars"},
                {"self"},
                {"speechSynthesis"},
                {"status"},
                {"statusbar"},
                {"stop"},
                {"toolbar"},
                {"top"},
                {"webkitURL"},
                {"window"},
            };
            return kGlobals;
        }

        // Mutex protecting the cached processed-globals result.
        // Only acquired when no user defines are present (the common
        // case), so contention is minimal.
        static std::mutex sProcessedGlobalsMutex;

        // Lazily-populated cache of the ProcessedDefines result when no
        // user-defined overrides are active.  Avoids rebuilding the
        // lookup tables on every call.
        static std::optional<ProcessedDefines> sProcessedGlobals;

    }

    // MergeDefineData
    // ----------------
    // Combines an older define with a newer one.  The newer value always
    // wins, but flags are OR-ed together so that optimisation bits from
    // either definition are preserved.
    //
    // Input:  old = {KeyParts=["a"], Flags=kCanBeRemovedIfUnused}
    //         new = {KeyParts=["a"], Flags=kCallCanBeUnwrappedIfUnused}
    // Output: {KeyParts=["a"], Flags=kCanBeRemovedIfUnused | kCallCanBeUnwrappedIfUnused}
    //
    // Edge case: when old has no expression (nullptr) and new does, the
    // new expression is used as-is.
    DefineData MergeDefineData(DefineData old, DefineData new_data) {
        new_data.Flags = new_data.Flags | old.Flags;
        return new_data;
    }

    // ProcessDefines
    // ---------------
    // Builds the complete lookup tables that Guchho consults during
    // bundling.  The process has four stages:
    //
    //   1. Seed the tables with all known JavaScript globals from
    //      GetKnownGlobals().  Single-name globals (e.g. "Math") go
    //      into IdentifierDefines; multi-name paths (e.g. "Math.PI")
    //      go into DotDefines, keyed by the last segment.
    //
    //   2. Add the special identifiers "undefined", "NaN", and
    //      "Infinity" as constant expressions.  These cannot be
    //      tree-shaken because they are language-level constants.
    //
    //   3. Merge user-specified defines on top.  Single-name defines
    //      override the corresponding IdentifierDefines entry; multi-
    //      name defines are matched by full key parts and merged via
    //      MergeDefineData.
    //
    //   4. Cache the result when no user defines were provided, so
    //      subsequent calls return the pre-built tables without work.
    //
    // Input:  userDefines = [{KeyParts=["DEBUG"], expr="true"}]
    // Output: IdentifierDefines contains "DEBUG" => expr="true"
    //         plus all known globals
    //
    // Input:  userDefines = {}
    // Output: cached result with only known globals and special IDs
    //
    // Thread safety: the cache is protected by sProcessedGlobalsMutex.
    // The function is safe to call from multiple threads; only the
    // cache write is serialised.
    ProcessedDefines ProcessDefines(const std::vector<DefineData>& userDefines) {
        bool hasUserDefines = !userDefines.empty();

        // Fast path: return the cached result when no user overrides exist.
        if (!hasUserDefines) {
            std::lock_guard<std::mutex> lock(sProcessedGlobalsMutex);
            if (sProcessedGlobals.has_value()) {
                return sProcessedGlobals.value();
            }
        }

        ProcessedDefines result;

        // Stage 1: seed with known globals
        for (const auto& parts : GetKnownGlobals()) {
            const std::string& tail = parts.back();
            if (parts.size() == 1) {
                result.IdentifierDefines[tail] = DefineData{{}, nullptr, DefineFlags::kCanBeRemovedIfUnused};
            } else {
                auto flags = DefineFlags::kCanBeRemovedIfUnused;
                if (parts[0] == "Symbol") {
                    flags = flags | DefineFlags::kIsSymbolInstance;
                }
                result.DotDefines[tail].push_back(DefineData{parts, nullptr, flags});
            }
        }

        // Stage 2: add special identifiers as constant expressions
        {
            auto expr = std::make_shared<DefineExpr>();
            expr->Constant = std::make_unique<guchho::javascript::EUndefined>();
            result.IdentifierDefines["undefined"] = DefineData{{}, std::move(expr), DefineFlags::kNone};
        }

        {
            auto expr = std::make_shared<DefineExpr>();
            expr->Constant = std::make_unique<guchho::javascript::ENumber>(
                guchho::javascript::ENumber{std::numeric_limits<double>::quiet_NaN()});
            result.IdentifierDefines["NaN"] = DefineData{{}, std::move(expr), DefineFlags::kNone};
        }

        {
            auto expr = std::make_shared<DefineExpr>();
            expr->Constant = std::make_unique<guchho::javascript::ENumber>(
                guchho::javascript::ENumber{std::numeric_limits<double>::infinity()});
            result.IdentifierDefines["Infinity"] = DefineData{{}, std::move(expr), DefineFlags::kNone};
        }

        // Stage 3: merge user-specified defines
        for (const auto& data : userDefines) {
            if (data.KeyParts.size() == 1) {
                const std::string& name = data.KeyParts[0];
                result.IdentifierDefines[name] = MergeDefineData(result.IdentifierDefines[name], data);
                continue;
            }

            const std::string& tail = data.KeyParts.back();
            auto& dotDefines = result.DotDefines[tail];
            bool found = false;

            for (auto& define : dotDefines) {
                if (helpers::StringArraysEqual(data.KeyParts, define.KeyParts)) {
                    define = MergeDefineData(define, data);
                    found = true;
                    break;
                }
            }

            if (!found) {
                dotDefines.push_back(data);
            }
        }

        // Stage 4: cache the result when no user overrides were applied
        if (!hasUserDefines) {
            std::lock_guard<std::mutex> lock(sProcessedGlobalsMutex);
            if (!sProcessedGlobals.has_value()) {
                sProcessedGlobals = result;
            }
            return sProcessedGlobals.value();
        }

        return result;
    }
    
}

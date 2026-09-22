#include "guchho/logger.hpp"

namespace guchho::logger {

    // The std::format-style template backing every entry in the MsgCat catalog.
    // Entries are assembled from per-subsystem catalogs; FormatMsg() renders a
    // template together with the arguments supplied at the call site.
    std::string_view MsgTemplate(MsgCat id)
    {
        switch (id) {
        case MsgCat::kBundler_PluginNotAbsolutePath: return "Plugin \"{}\" returned a non-absolute path in the \"file\" namespace: {}";
        case MsgCat::kBundler_PluginNonAbsolutePath: return "Plugin \"{}\" returned a non-absolute path: {} (set a namespace if this is not a file path)";
        case MsgCat::kBundler_DifferentPathCase: return "Use \"{}\" instead of \"{}\" to avoid issues with case-sensitive file systems";
        case MsgCat::kBundler_CannotReadFile: return "Cannot read file: {}";
        case MsgCat::kBundler_CannotReadFileWithError: return "Cannot read file \"{}\": {}";
        case MsgCat::kBundler_CouldNotLoadDataURL: return "Could not load data URL: {}";
        case MsgCat::kBundler_BundlingPhaseImportsNotSupported: return "Bundling {} imports with the \"{}\" output format is not supported";
        case MsgCat::kBundler_BundlingPhaseImportsNotSupportedUnlessExternal: return "Bundling with {} imports is not supported unless they are external";
        case MsgCat::kBundler_UnsupportedSourceMapCommentWithError: return "Unsupported source map comment: {}";
        case MsgCat::kBundler_UnsupportedSourceMapComment: return "Unsupported source map comment";
        case MsgCat::kBundler_UnsupportedSourceMapCommentScheme: return "Unsupported source map comment: URL scheme \"{}\" is not supported";
        case MsgCat::kBundler_UnsupportedSourceMapCommentHost: return "Unsupported source map comment: Host \"{}\" is not supported in file URL";
        case MsgCat::kBundler_UnsupportedSourceMapCommentResolveDir: return "Unsupported source map comment: Cannot resolve relative URL without a resolve directory";
        case MsgCat::kBundler_CannotReadFile_2: return "Cannot read file: {}";
        case MsgCat::kBundler_CannotReadFileWithError_2: return "Cannot read file \"{}\": {}";
        case MsgCat::kBundler_NoLoaderConfigured: return "No loader is configured for \"{}\" files: {}";
        case MsgCat::kBundler_DoNotKnowHowToLoadPath: return "Unable to determine how to load path: {}";
        case MsgCat::kBundler_CouldNotResolve: return "Could not resolve {}";
        case MsgCat::kBundler_ShouldBeMarkedExternal: return "\"{}\" should be marked as external for use with \"require.resolve\"";
        case MsgCat::kBundler_IgnoredDynamicImport: return "Importing \"{}\" was allowed even though it could not be resolved because dynamic import failures appear to be handled here:";
        case MsgCat::kBundler_DynamicImportHandlerNote: return "The handler for dynamic import failures is here:";
        case MsgCat::kBundler_PanicParsing: return "panic: unknown exception (while parsing \"{}\")";
        case MsgCat::kBundler_CannotInjectCopyLoader: return "Cannot inject \"{}\" with the \"copy\" loader without bundling enabled";
        case MsgCat::kBundler_FailedToReadDirectory: return "Failed to read directory \"{}\": {}";
        case MsgCat::kBundler_InjectedPathCannotBeExternal: return "The injected path \"{}\" cannot be marked as external";
        case MsgCat::kBundler_CouldNotResolveEntryPoint: return "Could not resolve \"{}\"";
        case MsgCat::kBundler_EntryPointCannotBeExternal: return "The entry point \"{}\" cannot be marked as external";
        case MsgCat::kBundler_ImportAssertionRequiresJSONLoader: return "The file \"{}\" was loaded with the \"{}\" loader";
        case MsgCat::kBundler_ImportAssertionRequiresJSONLoaderNote: return "This import assertion requires the loader to be \"json\" instead:";
        case MsgCat::kBundler_ReconfigureLoaderNote: return "Either reconfigure Guchho to use the \"json\" loader for this file, or remove this import assertion.";
        case MsgCat::kBundler_CannotUseComposesWith: return "Cannot use \"composes\" with \"{}\"";
        case MsgCat::kBundler_ComposesOnlyCSSNote: return "You can only use \"composes\" with CSS files and \"{}\" is not a CSS file (it was loaded with the \"{}\" loader).";
        case MsgCat::kBundler_CannotImportIntoCSS: return "Cannot import \"{}\" into a CSS file";
        case MsgCat::kBundler_CannotImportIntoCSSNote: return "An \"@import\" rule can only be used to import another CSS file and \"{}\" is not a CSS file (it was loaded with the \"{}\" loader).";
        case MsgCat::kBundler_CannotUseAsURL: return "Cannot use \"{}\" as a URL";
        case MsgCat::kBundler_CannotUseAsURLNote: return "You can't use a \"url()\" token to reference a CSS file, and \"{}\" is a CSS file (it was loaded with the \"{}\" loader).";
        case MsgCat::kBundler_CannotUseAsURLNoURL: return "Cannot use \"{}\" as a URL";
        case MsgCat::kBundler_CannotUseAsURLNoURLNote: return "You can't use a \"url()\" token to reference the file \"{}\" because it was loaded with the \"{}\" loader, which doesn't provide a URL to embed in the resulting CSS.";
        case MsgCat::kBundler_CannotImportWithoutOutputPath: return "Cannot import \"{}\" into a JavaScript file without an output path configured";
        case MsgCat::kBundler_SideEffectsExcludedFromArray: return "It was excluded from the \"sideEffects\" array in the enclosing \"package.json\" file:";
        case MsgCat::kBundler_SideEffectsFalse: return "\"sideEffects\" is false in the enclosing \"package.json\" file:";
        case MsgCat::kBundler_IgnoringImportNoSideEffects: return "Ignoring this import because \"{}\" was marked as having no side effects{}";
        case MsgCat::kBundler_TopLevelAwaitHere: return "The top-level await in \"{}\" is here:";
        case MsgCat::kBundler_UnexpectedInvalidIndex: return "Unexpected invalid index";
        case MsgCat::kBundler_ImportsFileHere: return "The file \"{}\" imports the file \"{}\" here:";
        case MsgCat::kBundler_RequireCallTopLevelAwait: return "This require call is not allowed because the imported file \"{}\" contains a top-level await";
        case MsgCat::kBundler_RequireCallTransitiveTLA: return "This require call is not allowed because the transitive dependency \"{}\" contains a top-level await";
        case MsgCat::kBundler_ImportMapReordered: return "Import map in \"{}\" was reordered to appear before module scripts";
        case MsgCat::kBundler_RefusingToOverwriteInput: return "Refusing to overwrite input file \"{}\"{}";
        case MsgCat::kBundler_TwoOutputFilesSamePath: return "Two output files share the same path but have different contents: {}";
        case MsgCat::kLinker_ComposesValueFirstNote: return "The first definition of \"{}\" is here:";
        case MsgCat::kLinker_ComposesValueSecondNote: return "The second definition of \"{}\" is here:";
        case MsgCat::kLinker_ComposesValueUndefinedNote: return "The specification of \"composes\" does not define an order when class declarations from separate files are composed together. The value of the \"{}\" property for \"{}\" may change unpredictably as the code is edited. Make sure that all definitions of \"{}\" for \"{}\" are in a single file.";
        case MsgCat::kLinker_ComposesValueUndefined: return "The value of \"{}\" in the \"{}\" class is undefined";
        case MsgCat::kLinker_StringAsNameNotSupported: return "Using the string \"{}\" as an {} name is not supported in {}";
        case MsgCat::kLinker_NoMatchingExport: return "No matching export in \"{}\" for import \"{}\"";
        case MsgCat::kLinker_CycleWhileResolving: return "Detected cycle while resolving import \"{}\"";
        case MsgCat::kLinker_OneMatchingExportNote: return "One matching export is here:";
        case MsgCat::kLinker_AnotherMatchingExportNote: return "Another matching export is here:";
        case MsgCat::kLinker_ImportAlwaysUndefinedMultipleMatches: return "Import \"{}\" will always be undefined because there are multiple matching exports";
        case MsgCat::kLinker_AmbiguousImportMultipleMatches: return "Ambiguous import \"{}\" has multiple matching exports";
        case MsgCat::kLinker_UseLocalCSSLoaderHint: return "Use the \"local-css\" loader for \"{}\" to enable local names.";
        case MsgCat::kLinker_UseLocalSelectorHint: return "Use the \":local\" selector to change \"{}\" into a local name.";
        case MsgCat::kLinker_CannotUseGlobalNameWithComposes: return "Cannot use global name \"{}\" with \"composes\"";
        case MsgCat::kLinker_GlobalNameDefinedHereNote: return "The global name \"{}\" is defined here:";
        case MsgCat::kLinker_NameNeverAppearsIn: return "The name \"{}\" never appears in \"{}\"";
        case MsgCat::kLinker_AmbiguousReexport: return "Re-export of \"{}\" in \"{}\" is ambiguous and has been removed";
        case MsgCat::kLinker_OneDefinitionFromNote: return "One definition of \"{}\" comes from \"{}\" here:";
        case MsgCat::kLinker_AnotherDefinitionFromNote: return "Another definition of \"{}\" comes from \"{}\" here:";
        case MsgCat::kLinker_CannotTraverseDirectoryToChunk: return "Cannot traverse from directory \"{}\" to chunk \"{}\"";
        case MsgCat::kLinker_CircularImport: return "Internal error: generated chunks contain a circular import";
        case MsgCat::kSourceMap_Invalid: return "Invalid source map";
        case MsgCat::kSourceMap_ExpectedOffsetObject: return "Expected \"offset\" to be an object";
        case MsgCat::kSourceMap_ExpectedMapObject: return "Expected \"map\" to be an object";
        case MsgCat::kSourceMap_ExpectedSectionsArray: return "Expected \"sections\" to be an array";
        case MsgCat::kSourceMap_BadMappings: return "Bad \"mappings\" data in source map at character {}: {}";
        case MsgCat::kJS_NewlineBeforeArrow: return "Unexpected newline before \"=>\"";
        case MsgCat::kJS_NewlineAfterAsync: return "Unexpected newline after \"async\"";
        case MsgCat::kJS_NewlineAfterType: return "Unexpected newline after \"type\"";
        case MsgCat::kJS_DecoratorsNotValidHere: return "Decorators are not valid here";
        case MsgCat::kJS_MultipleDefaultClauses: return "Multiple default clauses are not allowed";
        case MsgCat::kJS_AwaitOutsideAsync: return "Cannot use \"await\" outside an async function";
        case MsgCat::kJS_LetWrappedInParens: return "\"let\" must be wrapped in parentheses to be used as an expression here:";
        case MsgCat::kJS_UsingDeclarationsNotAllowed: return "\"using\" declarations are not allowed here";
        case MsgCat::kJS_AwaitUsingDeclarationsNotAllowed: return "\"await using\" declarations are not allowed here";
        case MsgCat::kJS_ReturnNotUsableHere: return "A return statement cannot be used here:";
        case MsgCat::kJS_NewlineAfterThrow: return "Unexpected newline after \"throw\"";
        case MsgCat::kJS_UnexpectedInterface: return "Unexpected \"interface\"";
        case MsgCat::kJS_ExpressionNotReturned: return "The following expression is not returned because of an automatically-inserted semicolon";
        case MsgCat::kJS_DecoratorsOnConstructors: return "Decorators are not allowed on class constructors";
        case MsgCat::kJS_DecoratorsOnlyClassDeclarations: return "TypeScript experimental decorators can only be used with class declarations";
        case MsgCat::kJS_DecoratorsNotClassExpression: return "This is a class expression, not a class declaration:";
        case MsgCat::kJS_EnableExperimentalDecoratorsNote: return "You can enable experimental decorators by adding \"experimentalDecorators\": true to your \"tsconfig.json\" file.";
        case MsgCat::kJS_WrapDecoratorInParensNote: return "Wrap this decorator in parentheses to allow arbitrary expressions:";
        case MsgCat::kJS_DotNotAllowedAfterDecoratorCall: return "JavaScript decorator syntax does not allow \".\" after a call expression";
        case MsgCat::kJS_MultipleConstructors: return "Classes cannot contain more than one constructor";
        case MsgCat::kJS_AwaitAsIdentifier: return "Cannot use \"await\" as an identifier here:";
        case MsgCat::kJS_CannotUseNameIdentifier_2: return "Cannot use \"{}\" as an identifier here:";
        case MsgCat::kJS_TSDecoratorsPrivateIdentifier: return "TypeScript experimental decorators cannot be used on private identifiers";
        case MsgCat::kJS_DeclarePrivateIdentifier: return "\"declare\" cannot be used with a private identifier";
        case MsgCat::kJS_DeclareIndexSignature: return "\"declare\" cannot be used with an index signature";
        case MsgCat::kJS_InvalidFieldName: return "Invalid field name \"{}\"";
        case MsgCat::kJS_DeclareCannotBeUsedWith: return "\"declare\" cannot be used with a {}";
        case MsgCat::kJS_ConstructorCannotBeGetter: return "Class constructor cannot be a getter";
        case MsgCat::kJS_ConstructorCannotBeSetter: return "Class constructor cannot be a setter";
        case MsgCat::kJS_ConstructorCannotBeAsync: return "Class constructor cannot be an async function";
        case MsgCat::kJS_ConstructorCannotBeGenerator: return "Class constructor cannot be a generator";
        case MsgCat::kJS_InvalidStaticMethodName: return "Invalid static method name \"prototype\"";
        case MsgCat::kJS_GetterZeroArgs: return "Getter {} must have zero arguments";
        case MsgCat::kJS_SetterExactlyOneArg: return "Setter {} must have exactly one argument";
        case MsgCat::kJS_InvalidMethodName: return "Invalid method name \"{}\"";
        case MsgCat::kJS_AliasInvalidSurrogate: return "This {} alias is invalid because it contains the unpaired Unicode surrogate U+{}";
        case MsgCat::kJS_DuplicateImport: return "Duplicate import {} \"{}\"";
        case MsgCat::kJS_DuplicateImportFirstNote: return "The first \"{}\" was here:";
        case MsgCat::kJS_LetAsIdentifier: return "Cannot use \"let\" as an identifier here:";
        case MsgCat::kJS_ExpectedIdentifierAfterNamespace: return "Expected identifier after \"{}\" in namespaced JSX name";
        case MsgCat::kJS_UnexpectedDash: return "Unexpected \"-\"";
        case MsgCat::kJS_DuplicateJSXAttribute: return "Duplicate \"{}\" attribute in JSX element";
        case MsgCat::kJS_DuplicateJSXAttributeNote: return "The original \"{}\" attribute is here:";
        case MsgCat::kJS_UnexpectedBackslashInJSX: return "Unexpected backslash in JSX element";
        case MsgCat::kJS_BackslashEscapeNote: return "Quoted JSX attributes use XML-style escapes instead of JavaScript-style escapes:";
        case MsgCat::kJS_StringInsideBraceNote: return "Consider using a JavaScript string inside {{...}} instead of a quoted JSX attribute:";
        case MsgCat::kJS_ClosingTagMismatch: return "Unexpected closing {} does not match opening {}";
        case MsgCat::kJS_ClosingTagOpeningNote: return "The opening {} is here:";
        case MsgCat::kJS_UnexpectedEOFBeforeClosing: return "Unexpected end of file before a closing {}";
        case MsgCat::kJS_OpeningTagNote: return "The opening {} is here:";
        case MsgCat::kJS_UnexpectedCommaAfterRest: return "Unexpected \",\" after rest pattern";
        case MsgCat::kJS_ExpectedButFound: return "Expected identifier but found \"{}\"";
        case MsgCat::kCSS_Expected: return "Expected {}";
        case MsgCat::kCSS_ExpectedToGoWith: return "Expected {} to go with {}";
        case MsgCat::kCSS_UnbalancedNote: return "The unbalanced {} is here:";
        case MsgCat::kCSS_ExpectedButFound: return "Expected {} but found {}";
        case MsgCat::kCSS_Unexpected: return "Unexpected {}";
        case MsgCat::kCSS_AtCharsetMustBeFirst: return "\"@charset\" must be the first rule in the file";
        case MsgCat::kCSS_CharsetRuleBeforeNote: return "This rule cannot come before a \"@charset\" rule";
        case MsgCat::kCSS_UnsupportedCharsetWillUseUTF8: return "\"UTF-8\" will be used instead of unsupported charset \"{}\"";
        case MsgCat::kCSS_AtImportOnlyTopLevel: return "\"@import\" is only valid at the top level";
        case MsgCat::kCSS_AtImportRuleBeforeNote: return "This rule cannot come before an \"@import\" rule";
        case MsgCat::kCSS_AllAtImportsMustComeFirst: return "All \"@import\" rules must come first";
        case MsgCat::kCSS_KeyframesNameWithoutQuotes: return "Cannot use \"{}\" as a name for \"@keyframes\" without quotes";
        case MsgCat::kCSS_KeyframesQuoteNote: return "You can put \"{}\" in quotes to prevent it from becoming a CSS keyword.";
        case MsgCat::kCSS_AtNamespaceNotSupported: return "\"@namespace\" rules are not supported";
        case MsgCat::kCSS_LayerNameNotAllowed: return "\"{}\" cannot be used as a layer name";
        case MsgCat::kCSS_WhitespaceBothSidesOperator: return "The \"{}\" operator only works if there is whitespace on both sides";
        case MsgCat::kCSS_InfixOnlyOperator: return "\"{}\" can only be used as an infix operator, not a prefix operator";
        case MsgCat::kCSS_ExpectedColon: return "Expected \":\"";
        case MsgCat::kCSS_NotAKnownCSSProperty: return "{} is not a known CSS property";
        case MsgCat::kCSS_DidYouMeanNote: return "Did you mean {} instead?";
        case MsgCat::kCSS_ExpectedParenEndURLToken: return "Expected \")\" to end URL token";
        case MsgCat::kCSS_UnbalancedParenNote: return "The unbalanced \"(\" is here:";
        case MsgCat::kCSS_InvalidEscape: return "Invalid escape";
        case MsgCat::kCSS_NonPrintCharURLToken: return "Unexpected non-printable character in URL token";
        case MsgCat::kCSS_UnterminatedStringToken: return "Unterminated string token";
        case MsgCat::kCSS_ExpectedCommentTerminator_2: return "Expected \"*/\" to terminate multi-line comment";
        case MsgCat::kCSS_CommentStartsHere_2: return "The multi-line comment starts here:";
        case MsgCat::kCSS_CommentsUseSlashStar: return "Comments in CSS use \"/* ... */\" instead of \"//\"";
        case MsgCat::kCSS_UnexpectedCommaInside: return "Unexpected \",\" inside \"{}\"";
        case MsgCat::kCSS_UnexpectedCommaInsideNote: return "Different CSS tools behave differently in this case, so Guchho doesn't allow it. Either remove this comma or split this selector into multiple comma-separated \"{}\" selectors instead.";
        case MsgCat::kCSS_TypeSelectorAfterNesting: return "Cannot use type selector \"{}\" directly after nesting selector \"&\"";
        case MsgCat::kCSS_TypeSelectorAfterNestingNotes: return "CSS nesting syntax does not allow the \"&\" selector to come before a type selector. This restriction exists to avoid problems with SASS nesting, where the same syntax means something very different that has no equivalent in real CSS (appending a suffix to the parent selector).";
        case MsgCat::kCSS_TooMuchExpansion: return "CSS nesting is causing too much expansion";
        case MsgCat::kCSS_TooMuchExpansionNote: return "CSS nesting expansion was terminated because a rule was generated with {} selectors. This limit exists to prevent Guchho from using too much time and/or memory. Please change your CSS to use fewer levels of nesting.";
        case MsgCat::kCSS_NestingNotSupportedInTarget: return "Transforming this CSS nesting syntax is not supported in the configured target environment ({})";
        case MsgCat::kCSS_NestingNotSupportedInTargetNote: return "The nesting transform for this case must generate an \":is(...)\" but the configured target environment does not support the \":is\" pseudo-class.";
        case MsgCat::kCSS_ComposesNotValidHere: return "\"composes\" is not valid here";
        case MsgCat::kCSS_ComposesOnlySingleClass: return "\"composes\" only works inside single class selectors";
        case MsgCat::kCSS_ComposesOnlySingleClassNote: return "The parent selector is not a single class selector because of the syntax here:";
        case MsgCat::kCSS_ComposesInvalidLocation: return "\"composes\" declaration uses invalid location \"{}\"";
        case MsgCat::kCSS_UnexpectedToken: return "Unexpected {}";
        case MsgCat::kHTML_ParseError: return "An HTML parse error occurred while parsing the input";
        case MsgCat::kHTML_EmptyResourceURL: return "{} has an empty resource URL";

        // HTML lexer (html_lexer.cpp)
        case MsgCat::kHTML_ControlCharacterInInputStream: return "A control character was found in the input stream";
        case MsgCat::kHTML_NoncharacterInInputStream: return "A noncharacter was found in the input stream";
        case MsgCat::kHTML_SurrogateInInputStream: return "A surrogate was found in the input stream";
        case MsgCat::kHTML_NonVoidHtmlElementStartTagWithTrailingSolidus: return "A start tag of a non-void element had a trailing solidus";
        case MsgCat::kHTML_EndTagWithAttributes: return "An end tag had attributes";
        case MsgCat::kHTML_EndTagWithTrailingSolidus: return "An end tag had a trailing solidus";
        case MsgCat::kHTML_UnexpectedSolidusInTag: return "An unexpected solidus occurred in a tag";
        case MsgCat::kHTML_UnexpectedNullCharacter: return "The input contained a NULL character outside of a valid context";
        case MsgCat::kHTML_UnexpectedQuestionMarkInsteadOfTagName: return "The tokenizer encountered \"<?\" where a tag name was expected";
        case MsgCat::kHTML_InvalidFirstCharacterOfTagName: return "The first character of a tag name was not a valid start character";
        case MsgCat::kHTML_UnexpectedEqualsSignBeforeAttributeName: return "An \"=\" occurred before an attribute name";
        case MsgCat::kHTML_MissingEndTagName: return "An end tag was encountered without a tag name";
        case MsgCat::kHTML_UnexpectedCharacterInAttributeName: return "An unexpected character occurred in an attribute name";
        case MsgCat::kHTML_UnknownNamedCharacterReference: return "The named character reference was not recognized";
        case MsgCat::kHTML_MissingSemicolonAfterCharacterReference: return "The character reference was not terminated by a semicolon";
        case MsgCat::kHTML_UnexpectedCharacterAfterDoctypeSystemIdentifier: return "An unexpected character occurred after the doctype system identifier";
        case MsgCat::kHTML_UnexpectedCharacterInUnquotedAttributeValue: return "An unexpected character occurred in an unquoted attribute value";
        case MsgCat::kHTML_EofBeforeTagName: return "The input ended before a tag name was given";
        case MsgCat::kHTML_EofInTag: return "The input ended inside of a tag";
        case MsgCat::kHTML_MissingAttributeValue: return "An attribute lacked a value";
        case MsgCat::kHTML_MissingWhitespaceBetweenAttributes: return "Whitespace between attributes was missing";
        case MsgCat::kHTML_MissingWhitespaceAfterDoctypePublicKeyword: return "Whitespace after the doctype PUBLIC keyword was missing";
        case MsgCat::kHTML_MissingWhitespaceBetweenDoctypePublicAndSystemIdentifiers: return "Whitespace between the doctype public and system identifiers was missing";
        case MsgCat::kHTML_MissingWhitespaceAfterDoctypeSystemKeyword: return "Whitespace after the doctype SYSTEM keyword was missing";
        case MsgCat::kHTML_MissingQuoteBeforeDoctypePublicIdentifier: return "The doctype public identifier was not quoted";
        case MsgCat::kHTML_MissingQuoteBeforeDoctypeSystemIdentifier: return "The doctype system identifier was not quoted";
        case MsgCat::kHTML_MissingDoctypePublicIdentifier: return "The doctype public identifier was missing";
        case MsgCat::kHTML_MissingDoctypeSystemIdentifier: return "The doctype system identifier was missing";
        case MsgCat::kHTML_AbruptDoctypePublicIdentifier: return "The doctype public identifier ended unexpectedly";
        case MsgCat::kHTML_AbruptDoctypeSystemIdentifier: return "The doctype system identifier ended unexpectedly";
        case MsgCat::kHTML_CdataInHtmlContent: return "A CDATA section occurred outside of foreign content";
        case MsgCat::kHTML_IncorrectlyOpenedComment: return "A comment was opened incorrectly";
        case MsgCat::kHTML_EofInScriptHtmlCommentLikeText: return "The input ended inside script text that looks like an HTML comment";
        case MsgCat::kHTML_EofInDoctype: return "The input ended inside of a doctype";
        case MsgCat::kHTML_NestedComment: return "A comment contained a nested comment marker";
        case MsgCat::kHTML_AbruptClosingOfEmptyComment: return "An empty comment was closed abruptly";
        case MsgCat::kHTML_EofInComment: return "The input ended inside of a comment";
        case MsgCat::kHTML_IncorrectlyClosedComment: return "A comment was closed incorrectly";
        case MsgCat::kHTML_EofInCdata: return "The input ended inside of a CDATA section";
        case MsgCat::kHTML_AbsenceOfDigitsInNumericCharacterReference: return "A numeric character reference contained no digits";
        case MsgCat::kHTML_NullCharacterReference: return "A numeric character reference referenced the NULL character";
        case MsgCat::kHTML_SurrogateCharacterReference: return "A numeric character reference referenced a surrogate";
        case MsgCat::kHTML_CharacterReferenceOutsideUnicodeRange: return "A numeric character reference referenced a code point outside the Unicode range";
        case MsgCat::kHTML_ControlCharacterReference: return "A numeric character reference referenced a control character";
        case MsgCat::kHTML_NoncharacterCharacterReference: return "A numeric character reference referenced a noncharacter";
        case MsgCat::kHTML_MissingWhitespaceBeforeDoctypeName: return "Whitespace before the doctype name was missing";
        case MsgCat::kHTML_MissingDoctypeName: return "The doctype was missing a name";
        case MsgCat::kHTML_InvalidCharacterSequenceAfterDoctypeName: return "An invalid character sequence followed the doctype name";
        case MsgCat::kHTML_DuplicateAttribute: return "An attribute was duplicated";
        case MsgCat::kHTML_NonConformingDoctype: return "A doctype that is not conforming was parsed";
        case MsgCat::kHTML_MissingDoctype: return "A doctype was missing before the document's elements";
        case MsgCat::kHTML_MisplacedDoctype: return "A doctype occurred outside of the beginning of the document";
        case MsgCat::kHTML_EndTagWithoutMatchingOpenElement: return "An end tag had no corresponding open element";
        case MsgCat::kHTML_ClosingOfElementWithOpenChildElements: return "An element was closed while it still had open child elements";
        case MsgCat::kHTML_DisallowedContentInNoscriptInHead: return "Disallowed content occurred inside <noscript> in <head>";
        case MsgCat::kHTML_OpenElementsLeftAfterEof: return "The input ended while elements were still open";
        case MsgCat::kHTML_AbandonedHeadElementChild: return "Content was placed in <head> that belongs in <body>";
        case MsgCat::kHTML_MisplacedStartTagForHeadElement: return "A <head> start tag occurred where it is not allowed";
        case MsgCat::kHTML_NestedNoscriptInHead: return "A <noscript> element occurred inside another <noscript> in <head>";
        case MsgCat::kHTML_EofInElementThatCanContainOnlyText: return "The input ended inside an element that can only read text";
        case MsgCat::kCLI_FailedToReadMangleCache: return "Failed to read from mangle cache file \"{}\": {}";
        case MsgCat::kCLI_ExpectedTopLevelObject: return "Expected a top-level object in mangle cache file";
        case MsgCat::kCLI_ExpectedKeyStringOrFalse: return "Expected \"{}\" in mangle cache file to map to either a string or false";
        case MsgCat::kBundler_ImportingWithAttr: return "Importing with the \"{}\" attribute is not supported";
        case MsgCat::kBundler_ImportingWithTypeAttr: return "Importing with a type attribute of \"{}\" is not supported";
        case MsgCat::kLinker_PanicWhilePrinting: return " (while printing \"{}\")";
        case MsgCat::kLinker_PanicPrinting: return "panic: {}";
        case MsgCat::kLinker_UnknownExceptionPrinting: return "panic: unknown exception";

        // -------------------------------------------------------------------
        // JavaScript binder (js_bind.cpp)
        // -------------------------------------------------------------------
        case MsgCat::kJS_DuplicateCaseNever: return "This case clause will never be evaluated because it duplicates an earlier case clause";
        case MsgCat::kJS_DuplicateCaseMay: return "This case clause may never be evaluated because it likely duplicates an earlier case clause";
        case MsgCat::kJS_DuplicateCaseEarlierNote: return "The earlier case clause is here:";
        case MsgCat::kJS_AssignToDefineNote: return "The expression \"{}\" has been configured to be replaced with a constant using the \"define\" feature. If this expression is supposed to be a compile-time constant, then it doesn't make sense to assign to it here. Or if this expression is supposed to change at run-time, this \"define\" substitution should be removed.";
        case MsgCat::kJS_DuplicateProperty: return "Duplicate {} \"{}\" in {}";
        case MsgCat::kJS_DuplicatePropertyOriginalNote: return "The original {} \"{}\" is here:";
        case MsgCat::kJS_PrivateNameNotInEnclosing: return "Private name \"{}\" must be declared in an enclosing class";
        case MsgCat::kJS_NullishCoalescingAlwaysReturns: return "The \"??\" operator here will always return the {} operand";
        case MsgCat::kJS_NullishCoalescingLeftNote: return "The left operand of the \"??\" operator here will {} be null or undefined, so it will {} be returned. This usually indicates a bug in your code:";
        case MsgCat::kJS_LogicalAlwaysReturns: return "The \"{}\" operator here will always return the {} operand";
        case MsgCat::kJS_SuspiciousArrowNote: return "The \"=>\" symbol creates an arrow function expression in JavaScript. Did you mean to use the greater-than-or-equal-to operator \">=\" here instead?";
        case MsgCat::kJS_CommonJSVariableInESM: return "The CommonJS \"{}\" variable is treated as a global variable in an ECMAScript module and may not work as expected";
        case MsgCat::kJS_CommonJSVariableCJSNote: return "Node's package format requires that CommonJS files in a \"type\": \"module\" package use the \".cjs\" file extension.";
        case MsgCat::kJS_CommonJSVariableTSNote: return " If you are using TypeScript, you can use the \".cts\" file extension with Guchho instead.";
        case MsgCat::kJS_SuspiciousAssignToDefine: return "Suspicious assignment to defined constant \"{}\"";
        case MsgCat::kJS_SuspiciousAssignToDefineNote: return "The expression \"{}\" has been configured to be replaced with a constant using the \"define\" feature. If this expression is supposed to be a compile-time constant, then it doesn't make sense to assign to it here. Or if this expression is supposed to change at run-time, this \"define\" substitution should be removed.";
        case MsgCat::kJS_ImpossibleTypeof: return "The \"typeof\" operator will never evaluate to \"{}\"";
        case MsgCat::kJS_ImpossibleTypeofNullNote: return "The expression \"typeof x\" actually evaluates to \"object\" in JavaScript, not \"null\". You need to use \"x === null\" to test for null.";
        case MsgCat::kJS_EqualsNegativeZeroOperator: return "Comparison with -0 using the \"{}\" operator will also match 0";
        case MsgCat::kJS_EqualsNegativeZeroCase: return "Comparison with -0 using a case clause will also match 0";
        case MsgCat::kJS_EqualsNegativeZeroNote: return "Floating-point equality is defined such that 0 and -0 are equal, so \"x === -0\" returns true for both 0 and -0. You need to use \"Object.is(x, -0)\" instead to test for -0.";
        case MsgCat::kJS_EqualsNaNOperator: return "Comparison with NaN using the \"{}\" operator here is always {}";
        case MsgCat::kJS_EqualsNaNSwitchCase: return "This case clause will never be evaluated because equality with NaN is always false";
        case MsgCat::kJS_EqualsNaNNote: return "Floating-point equality is defined such that NaN is never equal to anything, so \"x === NaN\" always returns false. You need to use \"Number.isNaN(x)\" instead to test for NaN.";
        case MsgCat::kJS_EqualsNewObjectOperator: return "Comparison using the \"{}\" operator here is always {}";
        case MsgCat::kJS_EqualsNewObjectSwitchCase: return "This case clause will never be evaluated because the comparison is always false";
        case MsgCat::kJS_EqualsNewObjectNote: return "Equality with a new object is always false in JavaScript because the equality operator tests object identity. You need to write code to compare the contents of the object instead. For example, use \"Array.isArray(x) && x.length === 0\" instead of \"x === []\" to test for an empty array.";
        case MsgCat::kJS_AssignToInjectedImport: return "Cannot assign to \"{}\" because it's an import from an injected file";
        case MsgCat::kJS_AssignToInjectedImportNote: return "The symbol \"{}\" was exported from \"{}\" here:";
        case MsgCat::kJS_CannotEscapeName: return "\"{}\" cannot be escaped in {} but you can set the charset to \"utf8\" to allow unescaped Unicode characters";
        case MsgCat::kJS_SymbolAlreadyDeclared: return "The symbol \"{}\" has already been declared";
        case MsgCat::kJS_SymbolOriginalDeclaredNote: return "The symbol \"{}\" was originally declared here:";
        case MsgCat::kJS_DuplicateFunctionNestedBlocks: return "Duplicate function declarations are not allowed in nested blocks {}. {}";
        case MsgCat::kJS_UnexpectedParenInRegexp: return "Unexpected \")\" in regular expression";
        case MsgCat::kJS_UnsupportedRegexp: return "{} in {}";
        case MsgCat::kJS_UnsupportedRegexpNote: return "This regular expression literal has been converted to a \"new RegExp()\" constructor to avoid generating code with a syntax error. However, you will need to include a polyfill for \"RegExp\" for your code to have the correct behavior at run-time.";
        case MsgCat::kJS_UnsupportedRegexpFlag: return "The regular expression flag \"{}\" is not available";
        case MsgCat::kJS_UnsupportedRegexpUnicodePropertyEscape: return "Unicode property escapes in regular expressions are not available";
        case MsgCat::kJS_UnsupportedRegexpNamedCaptureGroup: return "Named capture groups in regular expressions are not available";
        case MsgCat::kJS_UnsupportedRegexpLookbehind: return "Lookbehind assertions in regular expressions are not available";
        case MsgCat::kJS_NonDefaultJSONImportUndefined: return "Non-default import \"{}\" is undefined with a JSON import assertion";
        case MsgCat::kJS_UseStrictNonSimpleParamList: return "Cannot use a \"use strict\" directive in a function with a non-simple parameter list";
        case MsgCat::kJS_CannotAssignToImport: return "Cannot assign to import \"{}\"";
        case MsgCat::kJS_AssignToImportThrowNote: return "Imports are immutable in JavaScript. To modify the value of this import, you must export a setter function in the imported file{} and then import and call that function here instead.";
        case MsgCat::kJS_AssignToImportWillThrow: return "This assignment will throw because \"{}\" is an import";
        case MsgCat::kJS_IndirectRequire: return "Indirect calls to \"require\" will not be bundled";
        case MsgCat::kJS_TopLevelThisUndefined: return "Top-level \"this\" will be replaced with undefined since this file is an ECMAScript module";
        case MsgCat::kJS_DuplicateFnDeclNested: return "Duplicate function declarations are not allowed in nested blocks {}. ";
        case MsgCat::kJS_DuplicateFnDeclModule: return "Duplicate top-level function declarations are not allowed in an ECMAScript module. ";
        case MsgCat::kJS_CannotAccessName: return "Cannot access \"{}\" here:";
        case MsgCat::kJS_NoContainingLabel: return "There is no containing label named \"{}\"";
        case MsgCat::kJS_LegacyHTMLCommentInESM: return "Legacy HTML single-line comments are not allowed in ECMAScript modules";
        case MsgCat::kJS_InvalidJSXRuntime: return "Invalid JSX runtime: \"{}\"";
        case MsgCat::kJS_InvalidJSXRuntimeNote: return "The JSX runtime can only be set to either \"classic\" or \"automatic\".";
        case MsgCat::kJS_JSXFactoryAutomatic: return "The JSX factory cannot be set when using React's \"automatic\" JSX transform";
        case MsgCat::kJS_InvalidJSXFactory: return "Invalid JSX factory: {}";
        case MsgCat::kJS_JSXFragmentAutomatic: return "The JSX fragment cannot be set when using React's \"automatic\" JSX transform";
        case MsgCat::kJS_InvalidJSXFragment: return "Invalid JSX fragment: {}";
        case MsgCat::kJS_JSXImportSourceAutomatic: return "The JSX import source cannot be set without also enabling React's \"automatic\" JSX transform";
        case MsgCat::kJS_JSXImportSourceAutomaticNote: return "You can enable React's \"automatic\" JSX transform for this file by using a \"@jsxRuntime automatic\" comment.";
        case MsgCat::kJS_JSXRuntimeInvalid: return "Invalid JSX runtime: \"{}\"";
        case MsgCat::kJS_JSXRuntimeInvalidNote: return "The JSX runtime can only be set to either \"classic\" or \"automatic\".";
        case MsgCat::kJS_CannotUseNameIdentifier: return "Cannot use \"{}\" as an identifier here:";
        case MsgCat::kJS_DecoratorError: return "JavaScript decorator syntax does not allow \"?.\" here";
        case MsgCat::kJS_DecoratorErrorCall: return "JavaScript decorator syntax does not allow \".\" after a call expression";
        case MsgCat::kJS_DecoratorWrapNote: return "Wrap this decorator in parentheses to allow arbitrary expressions:";
        case MsgCat::kJS_DecoratorExpressionPosition: return "TypeScript experimental decorators cannot be used in expression position";
        case MsgCat::kJS_ParameterDecoratorsExperimental: return "Parameter decorators only work when experimental decorators are enabled";
        case MsgCat::kJS_ParameterDecoratorsExperimentalNote: return "You can enable experimental decorators by adding \"experimentalDecorators\": true to your \"tsconfig.json\" file.";
        case MsgCat::kJS_ParameterDecoratorsInJS: return "Parameter decorators are not allowed in JavaScript";
        case MsgCat::kJS_BoundMultipleTimesInParamList: return "\"{}\" cannot be bound multiple times in the same parameter list";
        case MsgCat::kJS_BoundMultipleTimesOriginalNote: return "The name \"{}\" was originally bound here:";
        case MsgCat::kJS_NotDeclaredInThisFile: return "\"{}\" is not declared in this file";
        case MsgCat::kJS_CannotUseBreak: return "Cannot use \"break\" here:";
        case MsgCat::kJS_CannotContinueToLabel: return "Cannot continue to label \"{}\"";
        case MsgCat::kJS_CannotUseContinue: return "Cannot use \"continue\" here:";
        case MsgCat::kJS_DuplicateLabel: return "Duplicate label \"{}\"";
        case MsgCat::kJS_DuplicateLabelOriginalNote: return "The original label \"{}\" is here:";
        case MsgCat::kJS_TopLevelReturnInESM: return "Top-level return cannot be used inside an ECMAScript module";
        case MsgCat::kJS_InvalidAssignmentTarget: return "Invalid assignment target";
        case MsgCat::kJS_CannotUseNewTarget: return "Cannot use \"new.target\" here:";
        case MsgCat::kJS_LegacyOctalInTemplate: return "Legacy octal escape sequences cannot be used in template literals";
        case MsgCat::kJS_ImportMetaNotAvailable: return "\"import.meta\" is not available with the \"{}\" output format and will be empty";
        case MsgCat::kJS_ImportMetaNotAvailableNote: return "You need to set the output format to \"esm\" for \"import.meta\" to work correctly.";
        case MsgCat::kJS_KeyShorthandNotAllowedNote: return "Using \"key\" as a shorthand for \"key={true}\" is not allowed when using React's \"automatic\" JSX transform.";
        case MsgCat::kJS_ClassNameBeforeInit: return "Accessing class \"{}\" before initialization will throw";
        case MsgCat::kJS_AssignToConstant: return "Cannot assign to \"{}\" because it is a constant";
        case MsgCat::kJS_AssignToConstantThrowNote: return "The symbol \"{}\" was declared a constant here:";
        case MsgCat::kJS_AssignToConstantWillThrow: return "This assignment will throw because \"{}\" is a constant";
        case MsgCat::kJS_AssignToInjectedImportNote2: return "The symbol \"{}\" was exported from \"{}\" here:";
        case MsgCat::kJS_DuplicatePropFound: return "Duplicate \"{}\" prop found:";
        case MsgCat::kJS_DuplicatePropFoundNote: return "Both \"__source\" and \"__self\" are set automatically by Guchho when using React's \"automatic\" JSX transform. This duplicate prop may have come from a plugin.";
        case MsgCat::kJS_CannotAssignToInjectedImport2: return "Cannot assign to \"{}\" because it's an import from an injected file";
        case MsgCat::kJS_ReadOnlyPrivateMethod: return "Writing to read-only method \"{}\" will throw";
        case MsgCat::kJS_GetterOnlyPrivateProperty: return "Writing to getter-only property \"{}\" will throw";
        case MsgCat::kJS_SetterOnlyPrivateProperty: return "Reading from setter-only property \"{}\" will throw";
        case MsgCat::kJS_CannotAssignToPropertyOnImport: return "Cannot assign to property on import \"{}\"";
        case MsgCat::kJS_CannotAssignToPropertyOnImportNote: return "Imports are immutable in JavaScript. To modify the value of this import, you must export a setter function in the imported file and then import and call that function here instead.";
        case MsgCat::kJS_DeletePropertyOfSuper: return "Attempting to delete a property of \"super\" will throw a ReferenceError";
        case MsgCat::kJS_ProtoPropertyMoreThanOnce: return "Cannot specify the \"__proto__\" property more than once per object";
        case MsgCat::kJS_ProtoPropertyEarlierNote: return "The earlier \"__proto__\" property is here:";
        case MsgCat::kJS_ImportExpressionNotRecognized: return "This \"import()\" was not recognized because {}";
        case MsgCat::kJS_ImportNotBundledNotStringLiteral: return "This \"import\" expression will not be bundled because the argument is not a string literal";
        case MsgCat::kJS_DirectEval: return "Using direct eval with a bundler is not recommended and may cause problems";
        case MsgCat::kJS_DirectEvalNote: return "You can read more about direct eval and bundling in the documentation";
        case MsgCat::kJS_ConvertingRequireToESM: return "Converting require() calls to ESM is not supported";
        case MsgCat::kJS_RequireNotBundledNotStringLiteral: return "This \"require\" expression will not be bundled because the argument is not a string literal";
        case MsgCat::kJS_RequireCallNotBundledNotStringLiteral: return "This call to \"require\" will not be bundled because the argument is not a string literal";
        case MsgCat::kJS_RequireNotBundledArgCount: return "Calling require() with the wrong number of arguments is not supported";
        case MsgCat::kJS_RequireNotBundledArgCountNote: return "You must call require() with exactly one string argument";
        case MsgCat::kJS_RequireArgCountNote: return "To be bundled, a \"require\" call must have exactly 1 argument.";

        // -------------------------------------------------------------------
        // JavaScript parser (js_parser.cpp)
        // -------------------------------------------------------------------
        case MsgCat::kJS_SymbolAlreadyDeclared_2: return "The symbol \"{}\" has already been declared";
        case MsgCat::kJS_SymbolOriginalDeclared_2: return "The symbol \"{}\" was originally declared here:";
        case MsgCat::kJS_UnexpectedEquals: return "Unexpected \"=\"";
        case MsgCat::kJS_UnexpectedToken: return "Unexpected \"{}\"";
        case MsgCat::kJS_InvalidBindingPattern: return "Invalid binding pattern";
        case MsgCat::kJS_OperatorsWithoutParens: return "Cannot use \"{}\" with \"{}\" without parentheses";
        case MsgCat::kJS_OperatorsWithoutParensNote: return "Expressions of the form \"x {} y {} z\" are not allowed in JavaScript. You must disambiguate between \"(x {} y) {} z\" and \"x {} (y {} z)\" by adding parentheses.";
        case MsgCat::kJS_AwaitExpressionHere: return "Cannot use an \"await\" expression here:";
        case MsgCat::kJS_YieldExpressionHere: return "Cannot use a \"yield\" expression here:";
        case MsgCat::kJS_TopLevelAwaitNotSupported: return "Top-level await is currently not supported with the \"{}\" output format";
        case MsgCat::kJS_ArbitraryImportSecondArg: return "Using an arbitrary value as the second argument to \"import()\" is not possible in {}";
        case MsgCat::kJS_TopLevelAwaitNotAvailable: return "Top-level await is not available in {}";
        case MsgCat::kJS_DeferredImportsNotAvailable: return "Deferred imports are not available in {}";
        case MsgCat::kJS_SourcePhaseImportsNotAvailable: return "Source phase imports are not available in {}";
        case MsgCat::kJS_StringNamespaceIdentifier: return "Using a string as a module namespace identifier name is not supported in {}";
        case MsgCat::kJS_BigIntNotAvailable: return "Big integer literals are not available in {} and may crash at run-time";
        case MsgCat::kJS_ImportMetaNotAvailable_2: return "\"import.meta\" is not available in {} and will be empty";
        case MsgCat::kJS_FeatureNotAvailable: return "This feature is not available in {}";
        case MsgCat::kJS_TransformingNotSupported: return "Transforming {} to {} is not supported yet";
        case MsgCat::kJS_FeatureCannotBeUsedWhere: return "{} cannot be used {}";
        case MsgCat::kJS_FeatureCannotBeUsedWithESM: return "{} cannot be used with the \"esm\" output format due to strict mode";
        case MsgCat::kJS_ForLoopSingleDeclaration: return "for-{} loops must have a single declaration";
        case MsgCat::kJS_ForLoopNoInitializer: return "for-{} loop variables cannot have an initializer";
        case MsgCat::kJS_AsyncFnNamedAwait: return "An async function cannot be named \"await\"";
        case MsgCat::kJS_GeneratorFnNamedYield: return "A generator function expression cannot be named \"yield\"";
        case MsgCat::kJS_AssertKeywordNotSupported: return "The \"assert\" keyword is not supported in {}";
        case MsgCat::kJS_AssertKeywordNote: return "Did you mean to use \"with\" instead of \"assert\"?";
        case MsgCat::kJS_DeclarationInSingleStatement: return "Cannot use a declaration in a single-statement context";
        case MsgCat::kJS_UsingInSwitchCase: return "Cannot use a \"using\" declaration directly inside a switch case";

        // -------------------------------------------------------------------
        // JavaScript expression parser (js_parser_expression.cpp)
        // -------------------------------------------------------------------
        case MsgCat::kJS_RestArgDefaultInitializer: return "A rest argument cannot have a default initializer";
        case MsgCat::kJS_UnexpectedColon: return "Unexpected \":\"";
        case MsgCat::kJS_UnexpectedEllipsis: return "Unexpected \"...\"";
        case MsgCat::kJS_ForLoopAsyncOf: return "For loop initializers cannot start with \"async of\"";
        case MsgCat::kJS_NameMustBeInitialized: return "The {} \"{}\" must be initialized";
        case MsgCat::kJS_ThisMustBeInitialized: return "This {} must be initialized";
        case MsgCat::kJS_UnexpectedSuper: return "Unexpected \"super\"";
        case MsgCat::kJS_CannotUseThis: return "Cannot use \"this\" here:";
        case MsgCat::kJS_AwaitCannotBeUsed: return "The keyword \"await\" cannot be used here:";
        case MsgCat::kJS_AwaitCannotBeEscaped: return "The keyword \"await\" cannot be escaped";
        case MsgCat::kJS_YieldCannotBeUsed: return "The keyword \"yield\" cannot be used here:";
        case MsgCat::kJS_YieldCannotBeEscaped: return "The keyword \"yield\" cannot be escaped";
        case MsgCat::kJS_YieldWithoutParens: return "Cannot use a \"yield\" expression here without parentheses:";
        case MsgCat::kJS_YieldOutsideGenerator: return "Cannot use \"yield\" outside a generator function";
        case MsgCat::kJS_DeletePrivateName: return "Deleting the private name \"{}\" is forbidden";
        case MsgCat::kJS_JSXNotEnabled: return "The JSX syntax extension is not currently enabled";
        case MsgCat::kJS_JSXNotEnabledNote: return "You can enable JSX by adding a \"/* @jsxRuntime classic */\" comment at the top of the file or by adding \"jsx\": \"preserve\" to your \"tsconfig.json\" file.";
        case MsgCat::kJS_MTSCtsExtension: return "The file extension \"{}\" is not currently supported";
        case MsgCat::kJS_ImportWithoutParens: return "Cannot use an \"import\" expression here without parentheses:";
        case MsgCat::kJS_UnparenthesizedOptionalChainNew: return "Cannot use an unparenthesized optional chain inside the target of \"new\"";
        case MsgCat::kJS_TemplateLiteralsOptionalChainTag: return "Template literals cannot have an optional chain as a tag";
        case MsgCat::kJS_SuspiciousBangIn: return "Suspicious use of the \"!\" operator in \"!x in y\":";
        case MsgCat::kJS_SuspiciousBangInNote: return "This expression is always a boolean because the \"!\" operator binds tighter than \"in\". If you want to check that \"x\" is not in \"y\", you probably want \"!(x in y)\" instead:";
        case MsgCat::kJS_SuspiciousBangInstanceof: return "Suspicious use of the \"!\" operator in \"!x instanceof y\":";
        case MsgCat::kJS_SuspiciousBangInstanceofNote: return "This expression is always a boolean because the \"!\" operator binds tighter than \"instanceof\". If you want to check that \"x\" is not an instance of \"y\", you probably want \"!(x instanceof y)\" instead:";

        // -------------------------------------------------------------------
        // JavaScript import/export parser (js_parser_import.cpp)
        // -------------------------------------------------------------------
        case MsgCat::kJS_MultipleExportsSameName: return "Multiple exports with the same name \"{}\"";
        case MsgCat::kJS_MultipleExportsOriginalNote: return "The name \"{}\" was originally exported here:";
        case MsgCat::kJS_NonDefaultJSONImportWithAssertion: return "Cannot use non-default import \"{}\" with a JSON import assertion";
        case MsgCat::kJS_ImportNamespaceCrash: return "{} \"{}\"{} will crash at run-time because it's an import namespace object, not a {}";
        case MsgCat::kJS_ImportNamespaceDefaultImportNote: return "Consider changing \"{}\" to a default import instead:";
        case MsgCat::kJS_ImportNamespaceESModuleInteropNote: return "Make sure to enable TypeScript's \"esModuleInterop\" setting so that TypeScript's type checker generates an error when you try to do this. You can read more about this setting here: https://www.typescriptlang.org/tsconfig#esModuleInterop";

        // -------------------------------------------------------------------
        // TypeScript parser (ts_parser.cpp)
        // -------------------------------------------------------------------
        case MsgCat::kTS_UnexpectedConst: return "Unexpected \"const\"";
        case MsgCat::kTS_UnexpectedToken: return "Unexpected \"{}\"";
        case MsgCat::kTS_ModifierNotValid: return "The modifier \"{}\" is not valid here:";
        case MsgCat::kTS_ExpectedCommaAfterValueInEnum: return "Expected \",\" after \"{}\" in enum";
        case MsgCat::kTS_ExpectedCommaBeforeNextInEnum: return "Expected \",\" before \"{}\" in enum";

        // -------------------------------------------------------------------
        // JS legacy lexer (js_lexer.cpp)
        // -------------------------------------------------------------------
        case MsgCat::kJS_UnexpectedEOF: return "Unexpected end of file";
        case MsgCat::kJS_SyntaxError: return "Syntax error \"{}\"";
        case MsgCat::kJS_SyntaxErrorHex: return "Syntax error \"\\x{:02X}\"";
        case MsgCat::kJS_SyntaxErrorUnicode: return "Syntax error \"\\u{{{:x}}}\"";
        case MsgCat::kJS_SyntaxErrorQuote: return "Syntax error '\"'";
        case MsgCat::kJS_AwaitInAsyncFunction: return "\"await\" can only be used inside an \"async\" function";
        case MsgCat::kJS_ConsiderAddingAsyncNote: return "Consider adding the \"async\" keyword here:";
        case MsgCat::kJS_ExpectedWithSuffix: return "Expected {}{} but found {}";
        case MsgCat::kJS_UnexpectedXF: return "Unexpected {}{}";
        case MsgCat::kJS_CharNotValidInJSX: return "The character \"{}\" is not valid inside a JSX element";
        case MsgCat::kJS_TSXArrowDisambiguation: return "TypeScript's TSX syntax interprets arrow functions with a single generic type parameter as an opening JSX element. If you want it to be interpreted as an arrow function instead, you need to add a trailing comma after the type parameter to disambiguate:";
        case MsgCat::kJS_DidYouMeanEscape: return "Did you mean to escape it as {} instead?";
        case MsgCat::kJS_ExpectedCommentTerminator: return "Expected \"*/\" to terminate multi-line comment";
        case MsgCat::kJS_CommentStartsHere: return "The multi-line comment starts here:";
        case MsgCat::kJS_UnterminatedStringLiteral: return "Unterminated string literal";
        case MsgCat::kJS_TreatingAsLegacyHTMLComment: return "Treating \"{}\" as the start of a legacy HTML single-line comment";
        case MsgCat::kJS_JSONNoComments: return "JSON does not support comments";
        case MsgCat::kJS_JSONStringsDoubleQuotes: return "JSON strings must use double quotes";
        case MsgCat::kJS_InvalidIdentifier: return "Invalid identifier: {}";
        case MsgCat::kJS_UnterminatedRegexp: return "Unterminated regular expression";
        case MsgCat::kJS_DuplicateRegexpFlag: return "Duplicate flag \"{}\" in regular expression";
        case MsgCat::kJS_UnicodeEscapeOutOfRange: return "Unicode escape sequence is out of range";

        // -------------------------------------------------------------------
        // JSON parser (json_parser.cpp)
        // -------------------------------------------------------------------
        case MsgCat::kJSON_NoTrailingCommas: return "JSON does not support trailing commas";
        case MsgCat::kJSON_DuplicateKey: return "Duplicate key {} in object literal";
        case MsgCat::kJSON_DuplicateKeyOriginalNote: return "The original key {} is here:";

        // -------------------------------------------------------------------
        // Resolver (resolver.cpp)
        // -------------------------------------------------------------------
        case MsgCat::kResolver_CannotFindTSConfig: return "Cannot find tsconfig file \"{}\"";
        case MsgCat::kResolver_CannotReadFile: return "Cannot read file {}: {}";
        case MsgCat::kResolver_GlobPatternNoMatch: return "Glob pattern {} does not match any files";
        case MsgCat::kResolver_EmptyGlob: return "The glob pattern {} did not match any files";
        case MsgCat::kResolver_BaseConfigCycle: return "Base config file \"{}\" forms cycle";
        case MsgCat::kResolver_CannotFindBaseConfig: return "Cannot find base config file \"{}\"";
        case MsgCat::kResolver_CannotReadDirectory: return "Cannot read directory {}: {}";
        case MsgCat::kResolver_MainFieldIgnoredNeutral: return "The \"{}\" field here was ignored. Main fields must be configured explicitly when using the \"neutral\" platform.";
        case MsgCat::kResolver_MainFieldIgnoredList: return "The \"{}\" field here was ignored because the list of main fields to use is currently set to [{}].";
        case MsgCat::kResolver_PnPForbidsImport: return "The Yarn Plug'n'Play manifest forbids importing {} here because it's not listed as a dependency of this package:";
        case MsgCat::kResolver_PnPPeerDependency: return "The Yarn Plug'n'Play manifest says this package has a peer dependency on {}, but the package {} has not been installed:";

        // -------------------------------------------------------------------
        // package.json (package_json.cpp)
        // -------------------------------------------------------------------
        case MsgCat::kPackageJSON_KeysBothStartAndNotWithDot: return "Keys in \"exports\" must start with \".\" or not start with \".\"";
        case MsgCat::kPackageJSON_IncompatibleKeyNote: return "One key starts with \".\" and the other does not:";
        case MsgCat::kPackageJSON_DeadCondition: return "Condition is not reachable";
        case MsgCat::kPackageJSON_DeadConditionMessage: return "The {} {} here will never be used as {} after {}";
        case MsgCat::kPackageJSON_ValueMustBeStringOrArrayOrNull: return "This value must be a string, an object, an array, or null";
        case MsgCat::kPackageJSON_CannotReadFile: return "Cannot read file {}: {}";
        case MsgCat::kPackageJSON_InvalidTypeValue: return "{} is not a valid value for the \"type\" field";
        case MsgCat::kPackageJSON_TypeFieldMustBeString: return "The value for \"type\" must be a string";
        case MsgCat::kPackageJSON_BrowserMappingStringOrBoolean: return "Each \"browser\" mapping must be a string or a boolean";
        case MsgCat::kPackageJSON_ExpectedStringInArray: return "Expected string in array for \"sideEffects\"";
        case MsgCat::kPackageJSON_SideEffectsBooleanOrArray: return "The value for \"sideEffects\" must be a boolean or an array";
        case MsgCat::kPackageJSON_ImportsMustBeObject: return "The value for \"imports\" must be an object";
        case MsgCat::kPackageJSON_ImportsMapIgnoredInvalidSpecifier: return "This \"imports\" map was ignored because the module specifier {} is invalid:";
        case MsgCat::kPackageJSON_RemappedPathCouldNotBeResolved: return "The remapped path {} could not be resolved:";
        case MsgCat::kPackageJSON_ModuleSpecifierInvalid: return "The module specifier {} is invalid{}:";
        case MsgCat::kPackageJSON_PackageTargetInvalid: return "The package target {} is invalid{}:";
        case MsgCat::kPackageJSON_PathDisabledByPackageAuthor: return "The path {} cannot be imported from package {} because it was explicitly disabled by the package author here:";
        case MsgCat::kPackageJSON_PathNotExportedByPackage: return "The path {} is not exported by package {}:";
        case MsgCat::kPackageJSON_FileExportedAtPath: return "The file {} is exported at path {}:";
        case MsgCat::kPackageJSON_ImportFromToGetFile: return "Import from {} to get the file {}:";
        case MsgCat::kPackageJSON_PackageImportNotDefined: return "The package import {} is not defined in this \"imports\" map:";
        case MsgCat::kPackageJSON_ModuleNotFound: return "The module {} was not found on the file system:";
        case MsgCat::kPackageJSON_ImportingDirectoryForbidden: return "Importing the directory {} is forbidden by this package:";
        case MsgCat::kPackageJSON_PropertyKeyMakesDirectoryForbidden: return "The presence of {} here makes importing a directory forbidden:";
        case MsgCat::kPackageJSON_PathNotCurrentlyExportedByPackage: return "The path {} is not currently exported by package {}:";
        case MsgCat::kPackageJSON_NoConditionsMatch: return "None of the conditions in the package definition ({}) match any of the currently active conditions ({}):";
        case MsgCat::kPackageJSON_ConsiderEnablingCondition: return "Consider enabling the {} condition if this package expects it to be enabled. You can use {} to do that:";

        // -------------------------------------------------------------------
        // guchho.json (guchho_json.cpp)
        // -------------------------------------------------------------------
        case MsgCat::kGuchhoJSON_UnknownField: return "Unknown field {} in guchho config will be ignored";
        case MsgCat::kGuchhoJSON_InvalidFormat: return "Invalid format {} (expected \"esm\", \"cjs\", \"iife\", \"umd\", \"amd\", or \"system\")";
        case MsgCat::kGuchhoJSON_InvalidPlatform: return "Invalid platform {} (expected \"browser\", \"node\", or \"neutral\")";
        case MsgCat::kGuchhoJSON_InvalidSourcemap: return "Invalid sourcemap {} (expected \"linked\", \"external\", or \"inline\")";
        case MsgCat::kGuchhoJSON_UnsupportedDefine: return "Unsupported define {}";
        case MsgCat::kGuchhoJSON_UnsupportedDefineValue: return "Unsupported define value for {}";
        case MsgCat::kGuchhoJSON_UnsupportedCompoundDefine: return "Unsupported compound define {}";
        case MsgCat::kGuchhoJSON_UnsupportedCompoundDefineValue: return "Unsupported compound define value for {}";
        case MsgCat::kGuchhoJSON_InvalidLogLevel: return "Invalid log level {} (expected \"verbose\", \"debug\", \"info\", \"warning\", or \"error\")";
        case MsgCat::kGuchhoJSON_InvalidLogLevelValue: return "Invalid log level {}";
        case MsgCat::kGuchhoJSON_PluginsNotImplemented: return "Plugins are not yet implemented";
        case MsgCat::kGuchhoJSON_CannotReadFile: return "Cannot read file {}: {}";

        // -------------------------------------------------------------------
        // tsconfig.json (tsconfig_json.cpp)
        // -------------------------------------------------------------------
        case MsgCat::kTSConfig_InvalidJSXMember: return "Invalid JSX member expression: {}";
        case MsgCat::kTSConfig_InvalidPattern: return "Invalid pattern {}, must have at most one \"*\" character";
        case MsgCat::kTSConfig_NonRelativePath: return "Non-relative path {} is not allowed when \"baseUrl\" is not set (did you forget a leading \"./\"?)";
        case MsgCat::kTSConfig_UnrecognizedTarget: return "Unrecognized target environment {}";
        case MsgCat::kTSConfig_InvalidImportsNotUsedAsValues: return "Invalid value for \"importsNotUsedAsValues\": {}";
        case MsgCat::kTSConfig_SubstitutionsShouldBeArray: return "Substitutions should be an array";
        case MsgCat::kTSConfig_OptionNotNested: return "Expected the {} option to be nested inside a \"compilerOptions\" object";

        // -------------------------------------------------------------------
        // guchho.config.js (guchho_config_js.cpp)
        // -------------------------------------------------------------------
        case MsgCat::kGuchhoConfig_FailedToSpawn: return "Failed to spawn {} to evaluate {}; falling back to a JSON config file";
        case MsgCat::kGuchhoConfig_FailedToEvaluate: return "Failed to evaluate {}{}; falling back to a JSON config file";
        case MsgCat::kGuchhoConfig_DidNotProduceConfigObject: return "{} did not produce a config object; falling back to a JSON config file";
        case MsgCat::kGuchhoConfig_NotJSONObject: return "{} did not evaluate to a JSON object; falling back to a JSON config file";

        // -------------------------------------------------------------------
        // Yarn PnP (yarnpnp.cpp)
        // -------------------------------------------------------------------
        case MsgCat::kResolver_CannotReadFile_2: return "Cannot read file {}: {}";

        // -------------------------------------------------------------------
        // API (api_impl.cpp)
        // -------------------------------------------------------------------
        case MsgCat::kAPI_InvalidVersion: return "Invalid version: {}";
        case MsgCat::kAPI_InvalidVersionNote: return "All version numbers passed to Guchho must be in the format \"X\", \"X.Y\", or \"X.Y.Z\", where X, Y, and Z are non-negative integers.";
        case MsgCat::kAPI_NotValidFeatureName: return "{} is not a valid feature name for the \"supported\" setting";
        case MsgCat::kAPI_NotValidRegexp: return "The {} setting is not a valid Go regular expression: {}";
        case MsgCat::kAPI_InvalidPath: return "Invalid {}: {}";
        case MsgCat::kAPI_MoreThanOneWildcard: return "External path {} cannot have more than one \"*\" wildcard";
        case MsgCat::kAPI_InvalidAliasSubstitution: return "Invalid alias substitution: {}";
        case MsgCat::kAPI_InvalidAliasName: return "Invalid alias name: {}";
        case MsgCat::kAPI_InvalidFileExtension: return "Invalid file extension: {}";
        case MsgCat::kAPI_InvalidJSX: return "Invalid JSX {}: {}";
        case MsgCat::kAPI_DefinedAsIdentifier: return "{} is defined as an identifier instead of a string (surround {} with quotes to get a string)";
        case MsgCat::kAPI_InvalidDefineValue: return "Invalid define value (must be an entity name or JS literal): {}";
        case MsgCat::kAPI_InvalidOutputExtension: return "Invalid output extension: {}";
        case MsgCat::kAPI_InvalidOutputExtensionValid: return "Invalid output extension: {} (valid: .css, .js)";
        case MsgCat::kAPI_InvalidFileType: return "Invalid {} file type: {} (valid: css, js)";
        case MsgCat::kAPI_KeepNamesCannotBeUsed: return "The \"keep names\" setting cannot be used with {}";
        case MsgCat::kAPI_KeepNamesCannotBeUsedNote: return "In this environment, the \"Function.prototype.name\" property is not configurable and assigning to it will throw an error. Either use a newer target environment or disable the \"keep names\" setting.";
        case MsgCat::kAPI_InvalidPluginFilterOnResolve: return "Invalid plug-in filter {} for onResolve plugin {}";
        case MsgCat::kAPI_InvalidPluginFilterOnLoad: return "Invalid plug-in filter {} for onLoad plugin {}";
        case MsgCat::kAPI_PluginMissingName: return "Plugin at index {} is missing a name";
        case MsgCat::kAPI_BuildCanceled: return "The build was canceled";
        case MsgCat::kAPI_UnexpectedFileCountWhenWritingToStdout: return "Internal error: did not expect to generate {} files when writing to stdout";
        case MsgCat::kAPI_FailedToCreateRealFS: return "Failed to create real filesystem for write";
        case MsgCat::kAPI_FailedToCreateOutputDirectory: return "Failed to create output directory: {}";
        case MsgCat::kAPI_FailedToWriteOutputFile: return "Failed to write to output file: {}";
        case MsgCat::kAPI_MustUseOutdirMultipleInputFiles: return "Must use \"outdir\" when there are multiple input files";
        case MsgCat::kAPI_MustUseOutdirCodeSplitting: return "Must use \"outdir\" when code splitting is enabled";
        case MsgCat::kAPI_CannotUseBothOutfileAndOutdir: return "Cannot use both \"outfile\" and \"outdir\"";
        case MsgCat::kAPI_CannotUseExternalSourceMap: return "Cannot use an external source map without an output path";
        case MsgCat::kAPI_CannotUseLinkedOrExternalLegalComments: return "Cannot use linked or external legal comments without an output path";
        case MsgCat::kAPI_CannotUseFileLoader: return "Cannot use the \"file\" loader without an output path";
        case MsgCat::kAPI_CannotUseExternalWithoutBundle: return "Cannot use \"external\" without \"bundle\"";
        case MsgCat::kAPI_CannotUseAliasWithoutBundle: return "Cannot use \"alias\" without \"bundle\"";
        case MsgCat::kAPI_SplittingOnlyESM: return "Splitting currently only works with the \"esm\" format";
        case MsgCat::kAPI_CannotProvideTsconfigBoth: return "Cannot provide \"tsconfig\" as both a raw string and a path";
        case MsgCat::kAPI_CannotTransformWithLinkedSourceMaps: return "Cannot transform with linked source maps";
        case MsgCat::kAPI_MustUseSourcefileWithSourcemap: return "Must use \"sourcefile\" with \"sourcemap\" to set the original file name";
        case MsgCat::kAPI_CannotTransformWithLinkedLegalComments: return "Cannot transform with linked legal comments";

            default:
                return "";
        }
    }

}

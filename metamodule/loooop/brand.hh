#pragma once

// Single source of truth for the MetaModule brand slug used by the native
// Loooop/Löp cores — both their register_module() calls and the faceplate
// paths derived in Loooop_info.hh / Lop_info.hh.
//
// This MUST stay equal to "slug"/"brand" in ../../plugin.json (and the brand
// in metamodule/plugin-mm.json), because the SDK's create_plugin() nests every
// faceplate asset under that slug (e.g. <slug>/Loooop/Loooop.png). On a brand
// rename there are exactly two edit sites: this macro and plugin.json
// (plus plugin-mm.json). The png_filename strings derive from FOOBAR_BRAND, so
// they can no longer fall out of sync.
//
// A macro (not a constexpr string_view) so it composes via string-literal
// concatenation into the constexpr png_filename initializers, e.g.
//   FOOBAR_BRAND "/Loooop/Loooop.png"
#define FOOBAR_BRAND "Foobar"

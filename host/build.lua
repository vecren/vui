local target = lito.target({ kind = "lib", name = "vui-host" })
local scanner = lito.tool("wayland-scanner")
local xml = "protocol/xdg-shell.xml"

lito.run({
  tool = scanner,
  cwd = ".",
  args = { "client-header", "@INPUT:1@", "@OUTPUT:1@" },
  inputs = { xml },
  outputs = { "include/xdg-shell-client-protocol.h" },
})
lito.run({
  tool = scanner,
  cwd = ".",
  args = { "private-code", "@INPUT:1@", "@OUTPUT:1@" },
  inputs = { xml },
  outputs = { "include/xdg-shell-protocol.inc" },
})
local text_xml = "protocol/text-input-unstable-v3.xml"
lito.run({
  tool = scanner,
  cwd = ".",
  args = { "client-header", "@INPUT:1@", "@OUTPUT:1@" },
  inputs = { text_xml },
  outputs = { "include/text-input-v3-client-protocol.h" },
})
lito.run({
  tool = scanner,
  cwd = ".",
  args = { "private-code", "@INPUT:1@", "@OUTPUT:1@" },
  inputs = { text_xml },
  outputs = { "include/text-input-v3-protocol.inc" },
})
lito.target_add_generated_include(target, "include")

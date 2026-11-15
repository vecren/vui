local scanner = lito.tool("wayland-scanner")
if not scanner then return end

local target = lito.target({ kind = "lib", name = "vui-host" })
local protocols = {
  { "xdg-shell", "xdg-shell" },
  { "text-input-unstable-v3", "text-input-v3" },
  { "fractional-scale-v1", "fractional-scale-v1" },
  { "viewporter", "viewporter" },
  { "xdg-output-unstable-v1", "xdg-output-unstable-v1" },
  { "xdg-decoration-unstable-v1", "xdg-decoration-unstable-v1" },
}
for _, protocol in ipairs(protocols) do
  for _, output in ipairs({
    { "client-header", "-client-protocol.h" },
    { "private-code", "-protocol.inc" },
  }) do
    lito.run({
      tool = scanner,
      cwd = ".",
      args = { output[1], "@INPUT:1@", "@OUTPUT:1@" },
      inputs = { "protocol/" .. protocol[1] .. ".xml" },
      outputs = { "include/" .. protocol[2] .. output[2] },
    })
  end
end
lito.target_add_generated_include(target, "include")

local spirv = require("@lito.spirv")
local target = lito.target({ kind = "lib", name = "vui-render" })
local compiler = lito.tool("glslang")
local inputs = {}
local content = {
  'module vui.render:shaders;',
  'import vui.render.spirv;',
  'namespace vui::gpu::shaders {',
}

for _, shader in ipairs({
  { name = "quad", source = "quad.vert", stage = "vertex" },
  { name = "rectangle", source = "rectangle.frag", stage = "fragment" },
  { name = "composite", source = "composite.frag", stage = "fragment" },
  { name = "glyph", source = "glyph.frag", stage = "fragment" },
  { name = "image", source = "image.frag", stage = "fragment" },
}) do
  local result = spirv.compile({
    backend = "glslang", tool = compiler, language = "glsl",
    source = "shaders/" .. shader.source, stage = shader.stage,
    target_env = "vulkan1.1", include_directories = { "shaders" },
    output = "shaders/" .. shader.source .. ".spv",
  })
  inputs[#inputs + 1] = result.spirv
  content[#content + 1] = "inline constexpr auto " .. shader.name .. " = decode({"
  content[#content + 1] = '#embed "@INPUT:' .. #inputs .. '@"'
  content[#content + 1] = "});"
end
content[#content + 1] = "}"

local source = lito.write({
  output = "src/shaders.cppm", inputs = inputs,
  content = table.concat(content, "\n") .. "\n",
})
lito.target_add_generated_source(target, source.output)

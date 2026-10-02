#!/usr/bin/env python3
"""Compile the checked-in Vulkan shaders into the embedded SPIR-V header."""
from pathlib import Path
import struct
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
output = ['module vui.render:shaders;', 'namespace vui::gpu::shaders {']
with tempfile.TemporaryDirectory(prefix='vui-shaders-') as directory:
    for source, name in [('quad.vert', 'quad'), ('rectangle.frag', 'rectangle'), ('composite.frag', 'composite'), ('glyph.frag', 'glyph'), ('image.frag', 'image')]:
        binary = Path(directory) / (source + '.spv')
        subprocess.run(['glslangValidator', '-V', '--target-env', 'vulkan1.1',
                        '-I' + str(root / 'shaders'), str(root / 'shaders' / source),
                        '-o', str(binary)], check=True)
        data = binary.read_bytes()
        words = struct.unpack('<' + 'I' * (len(data) // 4), data)
        output.append(f'inline constexpr unsigned int {name}[] = {{')
        for start in range(0, len(words), 8):
            output.append('    ' + ', '.join(f'0x{word:08x}' for word in words[start:start+8]) + ',')
        output.append('};')
output.append('}')
(root / 'src/shaders.cppm').write_text('\n'.join(output) + '\n')

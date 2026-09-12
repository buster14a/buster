from pathlib import Path
out=Path('../typeof-inputs'); out.mkdir(exist_ok=True)
shapes={'comma':('(0,',')'), 'parentheses':('(',')'), 'prefix':('*& ',''), 'mixed':('*(0,&(', '))')}
for name,(opening,closing) in shapes.items():
    for depth in [64,256,1024,4096,16384]:
        source='long x; typedef __typeof__('+opening*depth+'x'+closing*depth+') result;\n'
        (out/f'{name}-{depth}.c').write_text(source)
for depth in [64,256,1024,4096,16384]:
    source='struct Node { long value; } object; typedef __typeof__(('+'(0,'*depth+'&object'+')'*depth+')->value) inferred;\n'
    (out/f'postfix-{depth}.c').write_text(source)

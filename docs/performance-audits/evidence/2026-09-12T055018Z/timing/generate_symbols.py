from pathlib import Path
import sys
dest = Path(sys.argv[1]); dest.mkdir(parents=True, exist_ok=True)
for n in [8, 64, 512, 2048]:
    source = ''.join(f"static long local_{i} = {i+1};\nlong global_{i} = {i+3};\nlong *address_{i} = &global_{i};\nstatic long add_{i}(long x) {{ return x + local_{i}; }}\nlong probe_{i}(long x) {{ return add_{i}(x) + global_{i} + *address_{i}; }}\n" for i in range(n))
    (dest / f'symbols_{n}.c').write_text(source)

.text
add x0, x0, :lo12:5
add x1, x1, :lo12:0x1000-1
add w2, w3, :lo12:1
add x3, x4, :lo12:(0x1000-2)
add x7, x8, :lo12:0xfff
ldrb w3, [x2, :lo12:0xfff]
ldrh w3, [x2, :lo12:0x1006]
ldrh w0, [x1, :lo12:0x1ffe]
ldr w4, [x2, :lo12:(0x1010-4)]
ldr x1, [x0, :lo12:0x1008]
ldr x8, [x9, :lo12:0xff8]
ldr x8, [x9, :lo12:0x7ff8]
str q5, [x2, :lo12:0x1010]
str q0, [x1, :lo12:0xfff0]
prfm pldl1keep, [x2, :lo12:8]

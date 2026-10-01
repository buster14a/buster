# Phase-order diagnostic comparison

Revision: `c9a6d233f3da1b647309b248325f37b2bfd6e0e1`.

Primary tables use direct-SSA FAST only. Failed compilations retain diagnostic counts but are excluded from successful paired conclusions. No-step functions are outside the no-match denominator. Counts represent observed operations, not wall time or acceptance.

Step visits retain each pass's existing currency; DCE visits include its use-count and deletion work. D clear-values, D seed-rows, P block-visits and P sweeps are separate columns and must not be added as interchangeable operations. Alias resolution, initial replacement setup and compaction are not fully counted.

## Primary population and artifacts

| Source | Schedule | Status | Observed/admitted | Changed/no-match | P changed | F2 changed/no-match | IR rows | .text bytes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| interactions | FADP | 0 | 10/10 | 10/0 | 3 | 0/0 | 94 | 1020 |
| interactions | FAPD | 0 | 10/10 | 10/0 | 3 | 0/0 | 92 | 988 |
| interactions | FPAD | 0 | 10/10 | 10/0 | 3 | 0/0 | 92 | 988 |
| interactions | PFAD | 0 | 10/10 | 10/0 | 0 | 0/0 | 94 | 1372 |
| interactions | FADPFD | 0 | 10/10 | 10/0 | 3 | 0/10 | 92 | 988 |
| interactions | FADPfd | 0 | 10/10 | 10/0 | 3 | 0/3 | 92 | 988 |
| interactions | FADPFADP | 0 | 10/10 | 10/0 | 3 | 0/10 | 92 | 988 |
| interactions | FAPfD | 0 | 10/10 | 10/0 | 3 | 0/3 | 92 | 988 |
| canonical_fast | FADP | 0 | 7/7 | 6/1 | 0 | 0/0 | 138 | 1465 |
| canonical_fast | FAPD | 0 | 7/7 | 6/1 | 0 | 0/0 | 138 | 1465 |
| canonical_fast | FPAD | 0 | 7/7 | 6/1 | 0 | 0/0 | 138 | 1465 |
| canonical_fast | PFAD | 0 | 7/7 | 6/1 | 0 | 0/0 | 138 | 1465 |
| canonical_fast | FADPFD | 0 | 7/7 | 6/1 | 0 | 0/7 | 138 | 1465 |
| canonical_fast | FADPfd | 0 | 7/7 | 6/1 | 0 | 0/0 | 138 | 1465 |
| canonical_fast | FADPFADP | 0 | 7/7 | 6/1 | 0 | 0/7 | 138 | 1465 |
| canonical_fast | FAPfD | 0 | 7/7 | 6/1 | 0 | 0/0 | 138 | 1465 |
| string | FADP | 0 | 73/73 | 61/12 | 0 | 0/0 | 7703 | 91624 |
| string | FAPD | 0 | 73/73 | 61/12 | 0 | 0/0 | 7703 | 91624 |
| string | FPAD | 0 | 73/73 | 61/12 | 0 | 0/0 | 7703 | 91624 |
| string | PFAD | 0 | 73/73 | 61/12 | 0 | 0/0 | 7703 | 91624 |
| string | FADPFD | 0 | 73/73 | 61/12 | 0 | 0/73 | 7703 | 91624 |
| string | FADPfd | 0 | 73/73 | 61/12 | 0 | 0/0 | 7703 | 91624 |
| string | FADPFADP | 0 | 73/73 | 61/12 | 0 | 0/73 | 7703 | 91624 |
| string | FAPfD | 0 | 73/73 | 61/12 | 0 | 0/0 | 7703 | 91624 |
| integer | FADP | 0 | 9/9 | 9/0 | 0 | 0/0 | 307 | 2898 |
| integer | FAPD | 0 | 9/9 | 9/0 | 0 | 0/0 | 307 | 2898 |
| integer | FPAD | 0 | 9/9 | 9/0 | 0 | 0/0 | 307 | 2898 |
| integer | PFAD | 0 | 9/9 | 9/0 | 0 | 0/0 | 307 | 2898 |
| integer | FADPFD | 0 | 9/9 | 9/0 | 0 | 0/9 | 307 | 2898 |
| integer | FADPfd | 0 | 9/9 | 9/0 | 0 | 0/0 | 307 | 2898 |
| integer | FADPFADP | 0 | 9/9 | 9/0 | 0 | 0/9 | 307 | 2898 |
| integer | FAPfD | 0 | 9/9 | 9/0 | 0 | 0/0 | 307 | 2898 |
| backend_pressure | FADP | 0 | 64/64 | 64/0 | 0 | 0/0 | 79040 | 1396729 |
| backend_pressure | FAPD | 0 | 64/64 | 64/0 | 0 | 0/0 | 79040 | 1396729 |
| backend_pressure | FPAD | 0 | 64/64 | 64/0 | 0 | 0/0 | 79040 | 1396729 |
| backend_pressure | PFAD | 0 | 64/64 | 64/0 | 0 | 0/0 | 79040 | 1396729 |
| backend_pressure | FADPFD | 0 | 64/64 | 64/0 | 0 | 0/64 | 79040 | 1396729 |
| backend_pressure | FADPfd | 0 | 64/64 | 64/0 | 0 | 0/0 | 79040 | 1396729 |
| backend_pressure | FADPFADP | 0 | 64/64 | 64/0 | 0 | 0/64 | 79040 | 1396729 |
| backend_pressure | FAPfD | 0 | 64/64 | 64/0 | 0 | 0/0 | 79040 | 1396729 |
| control_flow | FADP | 0 | 64/64 | 64/0 | 0 | 0/0 | 38144 | 722937 |
| control_flow | FAPD | 0 | 64/64 | 64/0 | 0 | 0/0 | 38144 | 722937 |
| control_flow | FPAD | 0 | 64/64 | 64/0 | 0 | 0/0 | 38144 | 722937 |
| control_flow | PFAD | 0 | 64/64 | 64/0 | 0 | 0/0 | 38144 | 722937 |
| control_flow | FADPFD | 0 | 64/64 | 64/0 | 0 | 0/64 | 38144 | 722937 |
| control_flow | FADPfd | 0 | 64/64 | 64/0 | 0 | 0/0 | 38144 | 722937 |
| control_flow | FADPFADP | 0 | 64/64 | 64/0 | 0 | 0/64 | 38144 | 722937 |
| control_flow | FAPfD | 0 | 64/64 | 64/0 | 0 | 0/0 | 38144 | 722937 |
| large_function | FADP | 0 | 1/1 | 1/0 | 0 | 0/0 | 8195 | 123975 |
| large_function | FAPD | 0 | 1/1 | 1/0 | 0 | 0/0 | 8195 | 123975 |
| large_function | FPAD | 0 | 1/1 | 1/0 | 0 | 0/0 | 8195 | 123975 |
| large_function | PFAD | 0 | 1/1 | 1/0 | 0 | 0/0 | 8195 | 123975 |
| large_function | FADPFD | 0 | 1/1 | 1/0 | 0 | 0/1 | 8195 | 123975 |
| large_function | FADPfd | 0 | 1/1 | 1/0 | 0 | 0/0 | 8195 | 123975 |
| large_function | FADPFADP | 0 | 1/1 | 1/0 | 0 | 0/1 | 8195 | 123975 |
| large_function | FAPfD | 0 | 1/1 | 1/0 | 0 | 0/0 | 8195 | 123975 |
| many_functions | FADP | 0 | 512/512 | 512/0 | 0 | 0/0 | 3584 | 57330 |
| many_functions | FAPD | 0 | 512/512 | 512/0 | 0 | 0/0 | 3584 | 57330 |
| many_functions | FPAD | 0 | 512/512 | 512/0 | 0 | 0/0 | 3584 | 57330 |
| many_functions | PFAD | 0 | 512/512 | 512/0 | 0 | 0/0 | 3584 | 57330 |
| many_functions | FADPFD | 0 | 512/512 | 512/0 | 0 | 0/512 | 3584 | 57330 |
| many_functions | FADPfd | 0 | 512/512 | 512/0 | 0 | 0/0 | 3584 | 57330 |
| many_functions | FADPFADP | 0 | 512/512 | 512/0 | 0 | 0/512 | 3584 | 57330 |
| many_functions | FAPfD | 0 | 512/512 | 512/0 | 0 | 0/0 | 3584 | 57330 |
| symbol_table | FADP | 0 | 1/1 | 0/1 | 0 | 0/0 | 6146 | 43054 |
| symbol_table | FAPD | 0 | 1/1 | 0/1 | 0 | 0/0 | 6146 | 43054 |
| symbol_table | FPAD | 0 | 1/1 | 0/1 | 0 | 0/0 | 6146 | 43054 |
| symbol_table | PFAD | 0 | 1/1 | 0/1 | 0 | 0/0 | 6146 | 43054 |
| symbol_table | FADPFD | 0 | 1/1 | 0/1 | 0 | 0/1 | 6146 | 43054 |
| symbol_table | FADPfd | 0 | 1/1 | 0/1 | 0 | 0/0 | 6146 | 43054 |
| symbol_table | FADPFADP | 0 | 1/1 | 0/1 | 0 | 0/1 | 6146 | 43054 |
| symbol_table | FAPfD | 0 | 1/1 | 0/1 | 0 | 0/0 | 6146 | 43054 |
| tiny_startup | FADP | 0 | 1/1 | 0/1 | 0 | 0/0 | 4 | 46 |
| tiny_startup | FAPD | 0 | 1/1 | 0/1 | 0 | 0/0 | 4 | 46 |
| tiny_startup | FPAD | 0 | 1/1 | 0/1 | 0 | 0/0 | 4 | 46 |
| tiny_startup | PFAD | 0 | 1/1 | 0/1 | 0 | 0/0 | 4 | 46 |
| tiny_startup | FADPFD | 0 | 1/1 | 0/1 | 0 | 0/1 | 4 | 46 |
| tiny_startup | FADPfd | 0 | 1/1 | 0/1 | 0 | 0/0 | 4 | 46 |
| tiny_startup | FADPFADP | 0 | 1/1 | 0/1 | 0 | 0/1 | 4 | 46 |
| tiny_startup | FAPfD | 0 | 1/1 | 0/1 | 0 | 0/0 | 4 | 46 |
| unity | FADP | 0 | 5341/5341 | 4942/399 | 0 | 0/0 | 1361840 | 21638362 |
| unity | FAPD | 0 | 5341/5341 | 4942/399 | 0 | 0/0 | 1361840 | 21638362 |
| unity | FPAD | 0 | 5341/5341 | 4942/399 | 0 | 0/0 | 1361840 | 21638362 |
| unity | PFAD | 0 | 5341/5341 | 4942/399 | 0 | 0/0 | 1361840 | 21638362 |
| unity | FADPFD | 0 | 5341/5341 | 4942/399 | 0 | 0/5341 | 1361840 | 21638362 |
| unity | FADPfd | 0 | 5341/5341 | 4942/399 | 0 | 0/0 | 1361840 | 21638362 |
| unity | FADPFADP | 0 | 5341/5341 | 4942/399 | 0 | 0/5341 | 1361840 | 21638362 |
| unity | FAPfD | 0 | 5341/5341 | 4942/399 | 0 | 0/0 | 1361840 | 21638362 |
| cjson | FADP | 0 | 113/113 | 92/21 | 0 | 0/0 | 8672 | 54777 |
| cjson | FAPD | 0 | 113/113 | 92/21 | 0 | 0/0 | 8672 | 54777 |
| cjson | FPAD | 0 | 113/113 | 92/21 | 0 | 0/0 | 8672 | 54777 |
| cjson | PFAD | 0 | 113/113 | 92/21 | 0 | 0/0 | 8672 | 54777 |
| cjson | FADPFD | 0 | 113/113 | 92/21 | 0 | 0/113 | 8672 | 54777 |
| cjson | FADPfd | 0 | 113/113 | 92/21 | 0 | 0/0 | 8672 | 54777 |
| cjson | FADPFADP | 0 | 113/113 | 92/21 | 0 | 0/113 | 8672 | 54777 |
| cjson | FAPfD | 0 | 113/113 | 92/21 | 0 | 0/0 | 8672 | 54777 |

## Primary work and allocation

| Source | Schedule | F visits | A visits | D visits | P visits | D clear-values | D seed-rows | P blocks | P sweeps | Spills/reloads/copies | Frame bytes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| interactions | FADP | 129 | 129 | 245 | 66 | 110 | 129 | 48 | 15 | 32/14/14 | 240 |
| interactions | FAPD | 129 | 129 | 235 | 66 | 110 | 129 | 48 | 15 | 31/13/13 | 224 |
| interactions | FPAD | 129 | 129 | 235 | 66 | 110 | 129 | 48 | 15 | 31/13/13 | 224 |
| interactions | PFAD | 129 | 129 | 245 | 30 | 110 | 129 | 28 | 10 | 49/20/14 | 352 |
| interactions | FADPFD | 258 | 129 | 446 | 66 | 220 | 258 | 48 | 15 | 31/13/13 | 224 |
| interactions | FADPfd | 179 | 129 | 317 | 66 | 156 | 179 | 48 | 15 | 31/13/13 | 224 |
| interactions | FADPFADP | 258 | 258 | 446 | 75 | 220 | 258 | 76 | 25 | 31/13/13 | 224 |
| interactions | FAPfD | 179 | 129 | 235 | 66 | 110 | 129 | 48 | 15 | 31/13/13 | 224 |
| canonical_fast | FADP | 188 | 188 | 360 | 33 | 165 | 188 | 31 | 7 | 36/23/10 | 280 |
| canonical_fast | FAPD | 188 | 188 | 360 | 33 | 165 | 188 | 31 | 7 | 36/23/10 | 280 |
| canonical_fast | FPAD | 188 | 188 | 360 | 33 | 165 | 188 | 31 | 7 | 36/23/10 | 280 |
| canonical_fast | PFAD | 188 | 188 | 360 | 33 | 165 | 188 | 31 | 7 | 36/23/10 | 280 |
| canonical_fast | FADPFD | 376 | 188 | 676 | 33 | 330 | 376 | 31 | 7 | 36/23/10 | 280 |
| canonical_fast | FADPfd | 188 | 188 | 360 | 33 | 165 | 188 | 31 | 7 | 36/23/10 | 280 |
| canonical_fast | FADPFADP | 376 | 376 | 676 | 66 | 330 | 376 | 62 | 14 | 36/23/10 | 280 |
| canonical_fast | FAPfD | 188 | 188 | 360 | 33 | 165 | 188 | 31 | 7 | 36/23/10 | 280 |
| string | FADP | 8753 | 8753 | 17296 | 675 | 7339 | 8753 | 1079 | 73 | 1465/1058/1260 | 21608 |
| string | FAPD | 8753 | 8753 | 17296 | 675 | 7339 | 8753 | 1079 | 73 | 1465/1058/1260 | 21608 |
| string | FPAD | 8753 | 8753 | 17296 | 675 | 7339 | 8753 | 1079 | 73 | 1465/1058/1260 | 21608 |
| string | PFAD | 8753 | 8753 | 17296 | 675 | 7339 | 8753 | 1079 | 73 | 1465/1058/1260 | 21608 |
| string | FADPFD | 17506 | 8753 | 33542 | 675 | 14678 | 17506 | 1079 | 73 | 1465/1058/1260 | 21608 |
| string | FADPfd | 8753 | 8753 | 17296 | 675 | 7339 | 8753 | 1079 | 73 | 1465/1058/1260 | 21608 |
| string | FADPFADP | 17506 | 17506 | 33542 | 1350 | 14678 | 17506 | 2158 | 146 | 1465/1058/1260 | 21608 |
| string | FAPfD | 8753 | 8753 | 17296 | 675 | 7339 | 8753 | 1079 | 73 | 1465/1058/1260 | 21608 |
| integer | FADP | 368 | 368 | 708 | 9 | 304 | 368 | 48 | 9 | 63/38/49 | 664 |
| integer | FAPD | 368 | 368 | 708 | 9 | 304 | 368 | 48 | 9 | 63/38/49 | 664 |
| integer | FPAD | 368 | 368 | 708 | 9 | 304 | 368 | 48 | 9 | 63/38/49 | 664 |
| integer | PFAD | 368 | 368 | 708 | 9 | 304 | 368 | 48 | 9 | 63/38/49 | 664 |
| integer | FADPFD | 736 | 368 | 1355 | 9 | 608 | 736 | 48 | 9 | 63/38/49 | 664 |
| integer | FADPfd | 368 | 368 | 708 | 9 | 304 | 368 | 48 | 9 | 63/38/49 | 664 |
| integer | FADPFADP | 736 | 736 | 1355 | 18 | 608 | 736 | 96 | 18 | 63/38/49 | 664 |
| integer | FAPfD | 368 | 368 | 708 | 9 | 304 | 368 | 48 | 9 | 63/38/49 | 664 |
| backend_pressure | FADP | 95424 | 95424 | 231168 | 0 | 95360 | 95424 | 64 | 64 | 57920/76864/56704 | 153088 |
| backend_pressure | FAPD | 95424 | 95424 | 231168 | 0 | 95360 | 95424 | 64 | 64 | 57920/76864/56704 | 153088 |
| backend_pressure | FPAD | 95424 | 95424 | 231168 | 0 | 95360 | 95424 | 64 | 64 | 57920/76864/56704 | 153088 |
| backend_pressure | PFAD | 95424 | 95424 | 231168 | 0 | 95360 | 95424 | 64 | 64 | 57920/76864/56704 | 153088 |
| backend_pressure | FADPFD | 190848 | 95424 | 445952 | 0 | 190720 | 190848 | 64 | 64 | 57920/76864/56704 | 153088 |
| backend_pressure | FADPfd | 95424 | 95424 | 231168 | 0 | 95360 | 95424 | 64 | 64 | 57920/76864/56704 | 153088 |
| backend_pressure | FADPFADP | 190848 | 190848 | 445952 | 0 | 190720 | 190848 | 128 | 128 | 57920/76864/56704 | 153088 |
| backend_pressure | FAPfD | 95424 | 95424 | 231168 | 0 | 95360 | 95424 | 64 | 64 | 57920/76864/56704 | 153088 |
| control_flow | FADP | 41216 | 41216 | 95680 | 19456 | 36032 | 41216 | 11328 | 64 | 24768/20544/6208 | 109568 |
| control_flow | FAPD | 41216 | 41216 | 95680 | 19456 | 36032 | 41216 | 11328 | 64 | 24768/20544/6208 | 109568 |
| control_flow | FPAD | 41216 | 41216 | 95680 | 19456 | 36032 | 41216 | 11328 | 64 | 24768/20544/6208 | 109568 |
| control_flow | PFAD | 41216 | 41216 | 95680 | 19456 | 36032 | 41216 | 11328 | 64 | 24768/20544/6208 | 109568 |
| control_flow | FADPFD | 82432 | 41216 | 188288 | 19456 | 72064 | 82432 | 11328 | 64 | 24768/20544/6208 | 109568 |
| control_flow | FADPfd | 41216 | 41216 | 95680 | 19456 | 36032 | 41216 | 11328 | 64 | 24768/20544/6208 | 109568 |
| control_flow | FADPFADP | 82432 | 82432 | 188288 | 38912 | 72064 | 82432 | 22656 | 128 | 24768/20544/6208 | 109568 |
| control_flow | FAPfD | 41216 | 41216 | 95680 | 19456 | 36032 | 41216 | 11328 | 64 | 24768/20544/6208 | 109568 |
| large_function | FADP | 9219 | 9219 | 20484 | 0 | 9218 | 9219 | 1 | 1 | 5122/6143/5120 | 16400 |
| large_function | FAPD | 9219 | 9219 | 20484 | 0 | 9218 | 9219 | 1 | 1 | 5122/6143/5120 | 16400 |
| large_function | FPAD | 9219 | 9219 | 20484 | 0 | 9218 | 9219 | 1 | 1 | 5122/6143/5120 | 16400 |
| large_function | PFAD | 9219 | 9219 | 20484 | 0 | 9218 | 9219 | 1 | 1 | 5122/6143/5120 | 16400 |
| large_function | FADPFD | 18438 | 9219 | 39944 | 0 | 18436 | 18438 | 1 | 1 | 5122/6143/5120 | 16400 |
| large_function | FADPfd | 9219 | 9219 | 20484 | 0 | 9218 | 9219 | 1 | 1 | 5122/6143/5120 | 16400 |
| large_function | FADPFADP | 18438 | 18438 | 39944 | 0 | 18436 | 18438 | 2 | 2 | 5122/6143/5120 | 16400 |
| large_function | FAPfD | 9219 | 9219 | 20484 | 0 | 9218 | 9219 | 1 | 1 | 5122/6143/5120 | 16400 |
| many_functions | FADP | 4096 | 4096 | 8192 | 0 | 3584 | 4096 | 512 | 512 | 2048/2048/1024 | 8192 |
| many_functions | FAPD | 4096 | 4096 | 8192 | 0 | 3584 | 4096 | 512 | 512 | 2048/2048/1024 | 8192 |
| many_functions | FPAD | 4096 | 4096 | 8192 | 0 | 3584 | 4096 | 512 | 512 | 2048/2048/1024 | 8192 |
| many_functions | PFAD | 4096 | 4096 | 8192 | 0 | 3584 | 4096 | 512 | 512 | 2048/2048/1024 | 8192 |
| many_functions | FADPFD | 8192 | 4096 | 15872 | 0 | 7168 | 8192 | 512 | 512 | 2048/2048/1024 | 8192 |
| many_functions | FADPfd | 4096 | 4096 | 8192 | 0 | 3584 | 4096 | 512 | 512 | 2048/2048/1024 | 8192 |
| many_functions | FADPFADP | 8192 | 8192 | 15872 | 0 | 7168 | 8192 | 1024 | 1024 | 2048/2048/1024 | 8192 |
| many_functions | FAPfD | 4096 | 4096 | 8192 | 0 | 3584 | 4096 | 512 | 512 | 2048/2048/1024 | 8192 |
| symbol_table | FADP | 6146 | 6146 | 12291 | 0 | 6145 | 6146 | 1 | 1 | 2049/0/2053 | 16 |
| symbol_table | FAPD | 6146 | 6146 | 12291 | 0 | 6145 | 6146 | 1 | 1 | 2049/0/2053 | 16 |
| symbol_table | FPAD | 6146 | 6146 | 12291 | 0 | 6145 | 6146 | 1 | 1 | 2049/0/2053 | 16 |
| symbol_table | PFAD | 6146 | 6146 | 12291 | 0 | 6145 | 6146 | 1 | 1 | 2049/0/2053 | 16 |
| symbol_table | FADPFD | 12292 | 6146 | 24582 | 0 | 12290 | 12292 | 1 | 1 | 2049/0/2053 | 16 |
| symbol_table | FADPfd | 6146 | 6146 | 12291 | 0 | 6145 | 6146 | 1 | 1 | 2049/0/2053 | 16 |
| symbol_table | FADPFADP | 12292 | 12292 | 24582 | 0 | 12290 | 12292 | 2 | 2 | 2049/0/2053 | 16 |
| symbol_table | FAPfD | 6146 | 6146 | 12291 | 0 | 6145 | 6146 | 1 | 1 | 2049/0/2053 | 16 |
| tiny_startup | FADP | 4 | 4 | 7 | 0 | 3 | 4 | 1 | 1 | 2/0/2 | 16 |
| tiny_startup | FAPD | 4 | 4 | 7 | 0 | 3 | 4 | 1 | 1 | 2/0/2 | 16 |
| tiny_startup | FPAD | 4 | 4 | 7 | 0 | 3 | 4 | 1 | 1 | 2/0/2 | 16 |
| tiny_startup | PFAD | 4 | 4 | 7 | 0 | 3 | 4 | 1 | 1 | 2/0/2 | 16 |
| tiny_startup | FADPFD | 8 | 4 | 14 | 0 | 6 | 8 | 1 | 1 | 2/0/2 | 16 |
| tiny_startup | FADPfd | 4 | 4 | 7 | 0 | 3 | 4 | 1 | 1 | 2/0/2 | 16 |
| tiny_startup | FADPFADP | 8 | 8 | 14 | 0 | 6 | 8 | 2 | 2 | 2/0/2 | 16 |
| tiny_startup | FAPfD | 4 | 4 | 7 | 0 | 3 | 4 | 1 | 1 | 2/0/2 | 16 |
| unity | FADP | 1555073 | 1555073 | 3132444 | 98119 | 1339058 | 1555073 | 172564 | 5341 | 248181/201435/186143 | 6159384 |
| unity | FAPD | 1555073 | 1555073 | 3132444 | 98119 | 1339058 | 1555073 | 172564 | 5341 | 248181/201435/186143 | 6159384 |
| unity | FPAD | 1555073 | 1555073 | 3132444 | 98119 | 1339058 | 1555073 | 172564 | 5341 | 248181/201435/186143 | 6159384 |
| unity | PFAD | 1555073 | 1555073 | 3132444 | 98119 | 1339058 | 1555073 | 172564 | 5341 | 248181/201435/186143 | 6159384 |
| unity | FADPFD | 3110146 | 1555073 | 6080520 | 98119 | 2678116 | 3110146 | 172564 | 5341 | 248181/201435/186143 | 6159384 |
| unity | FADPfd | 1555073 | 1555073 | 3132444 | 98119 | 1339058 | 1555073 | 172564 | 5341 | 248181/201435/186143 | 6159384 |
| unity | FADPFADP | 3110146 | 3110146 | 6080520 | 196238 | 2678116 | 3110146 | 345128 | 10682 | 248181/201435/186143 | 6159384 |
| unity | FAPfD | 1555073 | 1555073 | 3132444 | 98119 | 1339058 | 1555073 | 172564 | 5341 | 248181/201435/186143 | 6159384 |
| cjson | FADP | 9707 | 9707 | 19280 | 335 | 8163 | 9707 | 1193 | 113 | 1213/899/937 | 6696 |
| cjson | FAPD | 9707 | 9707 | 19280 | 335 | 8163 | 9707 | 1193 | 113 | 1213/899/937 | 6696 |
| cjson | FPAD | 9707 | 9707 | 19280 | 335 | 8163 | 9707 | 1193 | 113 | 1213/899/937 | 6696 |
| cjson | PFAD | 9707 | 9707 | 19280 | 335 | 8163 | 9707 | 1193 | 113 | 1213/899/937 | 6696 |
| cjson | FADPFD | 19414 | 9707 | 37425 | 335 | 16326 | 19414 | 1193 | 113 | 1213/899/937 | 6696 |
| cjson | FADPfd | 9707 | 9707 | 19280 | 335 | 8163 | 9707 | 1193 | 113 | 1213/899/937 | 6696 |
| cjson | FADPFADP | 19414 | 19414 | 37425 | 670 | 16326 | 19414 | 2386 | 226 | 1213/899/937 | 6696 |
| cjson | FAPfD | 9707 | 9707 | 19280 | 335 | 8163 | 9707 | 1193 | 113 | 1213/899/937 | 6696 |

## FAPD versus FADP

| Source | Successful pair | Paired functions | Different function counts | IR row delta | P removal delta | .text delta | .text differs | Spill/reload/copy delta | Frame delta |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| interactions | True | 10 | 1 | -2 | 0 | -32 | True | -1/-1/-1 | -16 |
| canonical_fast | True | 7 | 0 | 0 | 0 | 0 | False | 0/0/0 | 0 |
| string | True | 73 | 0 | 0 | 0 | 0 | False | 0/0/0 | 0 |
| integer | True | 9 | 0 | 0 | 0 | 0 | False | 0/0/0 | 0 |
| backend_pressure | True | 64 | 0 | 0 | 0 | 0 | False | 0/0/0 | 0 |
| control_flow | True | 64 | 0 | 0 | 0 | 0 | False | 0/0/0 | 0 |
| large_function | True | 1 | 0 | 0 | 0 | 0 | False | 0/0/0 | 0 |
| many_functions | True | 512 | 0 | 0 | 0 | 0 | False | 0/0/0 | 0 |
| symbol_table | True | 1 | 0 | 0 | 0 | 0 | False | 0/0/0 | 0 |
| tiny_startup | True | 1 | 0 | 0 | 0 | 0 | False | 0/0/0 | 0 |
| unity | True | 5341 | 0 | 0 | 0 | 0 | False | 0/0/0 | 0 |
| cjson | True | 113 | 0 | 0 | 0 | 0 | False | 0/0/0 | 0 |

## Additional FAPfD bytes versus FAPD

No successful paired source has fewer .text bytes under FAPfD. This does not establish runtime or compile-time equivalence.

## Fixture symbol differences versus FADP

| Frontend | Allocator | Schedule | Symbol | Instruction delta | Byte delta | Stack-reference delta | Division delta |
| --- | --- | --- | --- | --- | --- | --- | --- |
| direct-ssa | fast | FAPD | dead_phi | -7 | -32 | -3 | 0 |
| direct-ssa | fast | FPAD | dead_phi | -7 | -32 | -3 | 0 |
| direct-ssa | fast | PFAD | dead_phi | 16 | 64 | 8 | 0 |
| direct-ssa | fast | PFAD | parameter_cap | 27 | 240 | 35 | 0 |
| direct-ssa | fast | PFAD | phi_identity | 0 | 48 | 8 | 0 |
| direct-ssa | fast | FADPFD | dead_phi | -7 | -32 | -3 | 0 |
| direct-ssa | fast | FADPfd | dead_phi | -7 | -32 | -3 | 0 |
| direct-ssa | fast | FADPFADP | dead_phi | -7 | -32 | -3 | 0 |
| direct-ssa | fast | FAPfD | dead_phi | -7 | -32 | -3 | 0 |
| direct-ssa | quality | FAPD | dead_phi | -7 | -32 | -3 | 0 |
| direct-ssa | quality | FPAD | dead_phi | -7 | -32 | -3 | 0 |
| direct-ssa | quality | PFAD | dead_phi | 16 | 64 | 8 | 0 |
| direct-ssa | quality | PFAD | parameter_cap | 27 | 240 | 35 | 0 |
| direct-ssa | quality | PFAD | phi_identity | 0 | 48 | 8 | 0 |
| direct-ssa | quality | FADPFD | dead_phi | -7 | -32 | -3 | 0 |
| direct-ssa | quality | FADPfd | dead_phi | -7 | -32 | -3 | 0 |
| direct-ssa | quality | FADPFADP | dead_phi | -7 | -32 | -3 | 0 |
| direct-ssa | quality | FAPfD | dead_phi | -7 | -32 | -3 | 0 |
| memory-form | fast | FAPD | dead_phi | -7 | -32 | -3 | 0 |
| memory-form | fast | FPAD | dead_phi | -7 | -32 | -3 | 0 |
| memory-form | fast | PFAD | dead_phi | 16 | 64 | 8 | 0 |
| memory-form | fast | PFAD | parameter_cap | 21 | 192 | 28 | 0 |
| memory-form | fast | PFAD | phi_identity | 0 | 48 | 8 | 0 |
| memory-form | fast | FADPFD | dead_phi | -7 | -32 | -3 | 0 |
| memory-form | fast | FADPfd | dead_phi | -7 | -32 | -3 | 0 |
| memory-form | fast | FADPFADP | dead_phi | -7 | -32 | -3 | 0 |
| memory-form | fast | FADPFADP | parameter_cap | -6 | -48 | -7 | 0 |
| memory-form | fast | FAPfD | dead_phi | -7 | -32 | -3 | 0 |

Stack references count static operands mentioning rsp/rbp, including address calculations; they are not dynamic memory-traffic measurements.

Semantic comparisons: 24 matched, 0 mismatched, 0 unavailable.

Primary runs: 96; failed: 0; uninstrumented: 0; observed no-step functions: 0.

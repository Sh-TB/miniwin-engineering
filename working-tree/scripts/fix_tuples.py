import sys
with open(sys.argv[1]) as f: data=f.read().decode('latin-1', errors='replace'); data=data.replace('test_d', ', ' ').replace('test_c', ', ' ').replace('test_b', ' '); open(sys.argv[1], 'w', encoding='utf-8') as f: f.write(data); print('OK')

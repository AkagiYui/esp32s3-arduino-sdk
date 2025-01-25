from pathlib import Path
import sys
import json
import subprocess

def modify_platformio_mirror(new_content):
    """
    修改 platformio/__init__.py 中的镜像配置
    
    Args:
        new_content: 要替换的新内容（不包含中括号）
    """
    output = subprocess.check_output(['pio', 'system', 'info', '--json-output'], encoding='utf-8')
    info = json.loads(output)
    print(json.dumps(info, indent=4))

    python_path = Path(info['python_exe']['value'])
    print(f'Python path: {python_path}')
    penv_path = python_path.parent.parent
    print(f'Penv path: {penv_path}')
    lib_path = Path(penv_path, 'Lib')
    if Path(penv_path, 'lib').exists():
        lib_path = Path(penv_path, 'lib')
        if Path(penv_path, 'lib', 'python3.11').exists():
            lib_path = Path(penv_path, 'lib', 'python3.11')
    print(f'Lib path: {lib_path}')
    platformio_path = Path(lib_path, 'site-packages', 'platformio')
    init_path = Path(platformio_path, '__init__.py')
    print(f'Init path: {init_path}')
    # 在文件末尾添加新的内容
    with open(init_path, 'r') as f:
        content = f.read()

    mirror_config = f'__registry_mirror_hosts__ = [${new_content}]'
    check_config = f'__check_internet_hosts__ = [${new_content}]'
    content = content + '\n' + mirror_config + '\n' + check_config + '\n'

    with open(init_path, 'w') as f:
        f.write(content)
    print('new content:', content)

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print('使用方法: python3 modify_pio_registry_mirror.py "新的镜像配置内容"')
        print('示例: python3 modify_pio_registry_mirror.py "registry.example.com"')
        sys.exit(1)
    
    new_content = sys.argv[1]
    modify_platformio_mirror(new_content)
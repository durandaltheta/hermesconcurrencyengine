import subprocess 

def execute(cmd, cwd=None, print_func=print, env=env):
    process = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,cwd=cwd, env=env)

    for line in process.stdout:
        print_func(str(line, 'utf-8'), flush=True, end='')
    
    stdout, stderr = process.communicate()
    
    return process.returncode, stderr

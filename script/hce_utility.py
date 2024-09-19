import subprocess 

def execute(cmd, cwd=None, env=None):
    process = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,cwd=cwd, env=env)
    stdout = ''

    for line in process.stdout:
        fmt_line=str(line, 'utf-8')
        stdout+=fmt_line
        print(fmt_line, flush=True, end='')
    
    stub_stdout, stderr = process.communicate()
    
    return process.returncode, stderr, stdout

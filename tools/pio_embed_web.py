#
# PlatformIO pre-build hook: пересобрать src/page_html.h из web/index.html.
#
# Запускается автоматически при `pio run`, чтобы прошивка всегда содержала
# актуальную версию веб-интерфейса.
#
Import("env")  # noqa: F821  (SCons)
import os
import subprocess
import sys

project_dir = env.subst("$PROJECT_DIR")
script = os.path.join(project_dir, "tools", "embed_web.py")
html = os.path.join(project_dir, "web", "index.html")

if os.path.exists(script) and os.path.exists(html):
    subprocess.check_call([sys.executable, script])
else:
    print("[pio_embed_web] web/index.html не найден - используется готовый src/page_html.h")

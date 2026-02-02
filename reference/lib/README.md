# lib/

이 디렉토리는 사용자 정의 라이브러리 모듈을 저장합니다.

## 사용 방법

1. 여기에 Python 모듈 파일을 작성합니다 (예: `mymodule.py`)
2. main.py에서 `import lib.mymodule` 또는 `from lib import mymodule`로 임포트합니다

## 예시

```python
# lib/sensors.py
def read_temperature():
    return 25.0

# main.py
from lib import sensors
temp = sensors.read_temperature()
```

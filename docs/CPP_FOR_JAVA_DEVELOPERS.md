# Java 개발자를 위한 C++/Qt 안내

Java 경험이 있다면 클래스, 인터페이스, 람다, 이벤트 기반 GUI와 컬렉션 개념을 그대로 활용할 수 있습니다. 가장 큰 차이는 객체 수명과 빌드 과정입니다.

## 개념 대응

| Java | C++/Qt |
|---|---|
| `String` | `QString` |
| `ArrayList<T>` | `std::vector<T>` 또는 `QList<T>` |
| 인터페이스 | 순수 가상 함수가 있는 추상 클래스 |
| 이벤트 리스너 | Qt signal/slot |
| 람다 | C++ 람다 |
| Gradle/Maven | CMake |
| JAR/JVM | 네이티브 실행 파일과 Qt 라이브러리 |
| 가비지 컬렉션 | RAII 및 Qt 부모-자식 소유권 |

## 먼저 배울 내용

1. 값, 포인터와 참조의 차이
2. `const`의 의미
3. 객체가 생성되고 제거되는 시점
4. 헤더 `.h`와 구현 `.cpp` 분리
5. 컴파일과 링크의 차이
6. Qt signal/slot과 이벤트 루프

```cpp
QString message = "hello";       // 값을 가진 지역 객체
QString *pointer = &message;      // 객체 주소를 가리키는 포인터
const QString &ref = message;     // 복사 없는 읽기 전용 참조
```

## 안전하게 시작하는 규칙

- 임의의 `new/delete`보다 지역 객체와 스마트 포인터를 우선합니다.
- 배열 대신 `std::vector` 또는 Qt 컨테이너를 사용합니다.
- C 문자열보다 `QString`과 `QByteArray`를 사용합니다.
- 소유권이 불분명한 포인터를 오래 보관하지 않습니다.
- Qt 자식 위젯은 부모나 레이아웃에 소유권이 전달되는지 확인합니다.
- Git과 파일 작업은 UI 스레드에서 동기 실행하지 않습니다.
- 컴파일러 경고를 방치하지 않습니다.


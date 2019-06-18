# 네트워크 프로그래밍 Select 모델 - 친구 관리 예제(+RapidJSON)
## 📢 개요
 셀렉트(Select) 모델을 사용 하나의 서버에 다수의 플레이어가 참여할 수 있는 네트워크 프로그램. 각각의 클라이언트끼리는 친구 관계를 맺을 수 있다. 서버에서는 클라이언트 간의 친구 관계를 저장하고 불러오는 등 관리 역할을 한다.

  ![capture](https://github.com/kbm0996/-Network-SelectModel-FriendManager/blob/master/Friend_mail_Readme/Capture.PNG)
  
  *\*figure 1. Server & Client & Dummy*
 
 네트워크 통신의 특성 상 패킷의 손실이나 각종 예기치 못한 일로 인해 서버와 각각의 클라이언트 간에 유저들의 위치 오차가 발생할 수 있다. 이는 추측 항법(推測航法, dead reckoning, dead reckoning navigation)이나 오차가 심해졌을 때 임의로 서버 측에서 정정하는 패킷을 보내서 해결해야만 한다.
 
 현재 이 프로그램에서는 단순히 셀렉트 모델의 동작 방식과 코드 구성을 알아보기 위한 간단한 소스코드이므로 그런 복잡한 코드가 없다. 하지만 실제로 온라인 서비스를 제공할 때에는 반드시 필요한 기능이다.

## 📌 동작 원리
 Select 모델은 사용하면 소켓 함수 호출이 성공할 수 있는 시점을 미리 알 수 있다. 따라서 소켓 함수 호출 시 조건이 만족되지 않아 생기는 문제를 해결할 수 있다. 소켓 모드에 따른 Select 모델 사용의 효과는 다음과 같다.

  · 블로킹 소켓 : 소켓 함수 호출 시 조건이 만족되지 않아 블로킹되는 상황을 막을 수 있다\
 
  · 넌블로킹 소켓 : 소켓 함수 호출 시 조건이 마족되지 않아 나중에 다시 호출해야 하는 상황을 막을 수 있다.

 다음 그림은 Select 모델의 동작 원리를 보여준다. Select 모델을 사용하려면 소켓 셋(socket set)을 준비해야 한다. 소켓 셋은 소켓 디스크립터의 집합을 의미하며, 호출할 함수의 종류에 따라 소켓을 적당한 셋에 넣어두어야 한다. 예를 들면, 어떤 소켓에 대해 recv() 함수를 호출하고 싶다면 읽기 셋에 넣고, send() 함수를 호출하고 싶다면 쓰기 셋에 넣으면 된다.
 
 ![capture](https://github.com/kbm0996/-Network-SelectModel-FriendManager/blob/master/Friend_mail_Readme/Table.PNG)
  
  *\*figure 2. Table*
  
  ![capture](https://github.com/kbm0996/-Network-SelectModel-FriendManager/blob/master/Friend_mail_Readme/PacketStructure.PNG)
  
  *\*figure 3. Packet Structure*

.code:
  PUSH 1
  PUSH 128
  MSTORE
  PUSH 0
  PUSH 128
  MLOAD
  GT
  PUSH [tag0]
  JUMPI
  PUSH 3
  PUSH 128
  MSTORE
  PUSH [tag1]
  JUMP
tag0: 
  PUSH 2
  PUSH 128
  MSTORE
tag1: 

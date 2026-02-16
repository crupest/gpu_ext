# llama

## gpt-oss-120b

nsys profile --trace=cuda,nvtx,osrt ~/workspace/llama.cpp/build/bin/llama-server -hf unsloth/gpt-oss-120b-GGUF:Q4_K_M -ncmoe 64

nsys profile --trace=cuda,cuda-hw  --cuda-event-trace=true --cuda-graph=graph  ~/workspace/llama.cpp/build/bin/llama-cli -hf unsloth/gpt-oss-120b-GGUF:Q4_K_M -ncmoe 64

nsys profile --trace=cuda,cuda-hw  --cuda-event-trace=true --cuda-graph=node  ~/workspace/llama.cpp/build/bin/llama-cli -m /home/yunwei37/.cache/llama.cpp/mradermacher_Qwen3-42B-A3B-2507-Thinking-Abliterated-uncensored-TOTAL-RECALL-v2-Medium-MASTER-CODER-i1-GGUF_Qwen3-42B-A3B-2507-Thinking-Abliterated-uncensored-TOTAL-RECALL-v2-Medium-MASTER-CODER.i1-Q4_K_M.gguf 

i1-Q6_K.gguf

nsys profile --trace=cuda,cuda-hw  --cuda-event-trace=true --cuda-graph=node  ~/workspace/llama.cpp/build/bin/llama-cli -m /home/yunwei37/.cache/llama.cpp/mradermacher_Qwen3-42B-A3B-2507-Thinking-Abliterated-uncensored-TOTAL-RECALL-v2-Medium-MASTER-CODER-i1-GGUF_Qwen3-42B-A3B-2507-Thinking-Abliterated-uncensored-TOTAL-RECALL-v2-Medium-MASTER-CODER.i1-Q6_K.gguf


nsys profile --trace=cuda,nvtx,osrt --cuda-graph=node llama.cpp/build/bin/llama-server -hf unsloth/gpt-oss-120b-GGUF:Q4_K_M -ncmoe 64

nsys profile --trace=cuda,nvtx,osrt llama.cpp/build/bin/llama-server -m /home/yunwei37/.cache/llama.cpp/mradermacher_Qwen3-42B-A3B-2507-Thinking-Abliterated-uncensored-TOTAL-RECALL-v2-Medium-MASTER-CODER-i1-GGUF_Qwen3-42B-A3B-2507-Thinking-Abliterated-uncensored-TOTAL-RECALL-v2-Medium-MASTER-CODER.i1-Q4_K_M.gguf -c 40000

nsys profile --trace=cuda,nvtx,osrt --cuda-graph=node llama.cpp/build/bin/llama-server -m /home/yunwei37/.cache/llama.cpp/mradermacher_Qwen3-42B-A3B-2507-Thinking-Abliterated-uncensored-TOTAL-RECALL-v2-Medium-MASTER-CODER-i1-GGUF_Qwen3-42B-A3B-2507-Thinking-Abliterated-uncensored-TOTAL-RECALL-v2-Medium-MASTER-CODER.i1-Q4_K_M.gguf -c 40000



 ~/workspace/llama.cpp/build/bin/llama-server  --gpt-oss-120b-default -ncmoe 32 
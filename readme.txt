
test_batch.c 是一个测试工具，可以批量对 op/ip + config 进行处理。


1. Linux 编译 test_batch (已经编译好了，可以不必重新编译)

  make lib
  编译完成后，在 source-1221 里面会有一个库文件：libconvert2b.so
  
  make 
  编译完成后，在当前目录里面会有一个可执行文件：test_batch.out



2. Linux 运行 test_batch

  按此设置环境变量
  export LD_LIBRARY_PATH=./source
 
  ./test_batch.out 
  会输出如下信息：
  Usage: test_batch <input_op_dir> <config_dir> <output_dir>

  也就是运行这个批量工具，需要指定三个文件目录，分别是：
  input_op_dir: 所有的 2B OP 文件所在的目录，注意，目录里面只能有 OP 2B，2BL 文件，不要有其他类型文件
  config_dir: 所有的 config 文件所在的目录，注意，目录里面不要有其他类型文件
  output_dir: 输出文件存放到此目录，这个目录要先手工新建好

  
3. windows 上面运行 (windows 版本 exe 已经编译)
  windows 无需编译，无需做任何设置。
  cmd 里面 输入 test_batch.exe 即可。 参数同linux版

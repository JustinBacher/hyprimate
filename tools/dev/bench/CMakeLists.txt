# Benchmark executable: links the production core library unchanged. Warnings are kept on; the
# operator new counting replacement lives only in this executable.
add_executable(hyprimate-bench BenchMain.cpp BenchGeometry.cpp BenchBubble.cpp BenchSmoke.cpp BenchSheets.cpp)
target_link_libraries(hyprimate-bench PRIVATE hyprimate-core Jolt)
target_compile_options(hyprimate-bench PRIVATE -Wall -Wextra)

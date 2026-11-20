# Benchmark executable: links the production core library unchanged. Warnings are kept on; the
# operator new counting replacement lives only in this executable.
add_executable(hyprimate-bench BenchMain.cpp BenchGeometry.cpp BenchBubble.cpp BenchSmoke.cpp BenchSheets.cpp)
target_link_libraries(hyprimate-bench PRIVATE hyprimate-core Jolt)
target_compile_options(hyprimate-bench PRIVATE -Wall -Wextra)

# Paced pose publication is not a throughput microbenchmark: the CPU scheduler and wall clock
# are part of the measurement. Keep it out of deterministic checksum/allocation comparisons.
add_executable(paper-cadence PaperCadence.cpp)
target_compile_features(paper-cadence PRIVATE cxx_std_23)
target_link_libraries(paper-cadence PRIVATE hyprimate-core)

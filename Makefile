

################################ Macros #################################

SHELL = /bin/sh
NVCC = nvcc
NVCCCFLAGS = -arch=sm_50 -std=c++14 -O3 -use_fast_math -lcublas
CUFLAGS = -x cu
OBJECTS = Helper.o ArffImporter.o Sigmoid.o HyperTangent.o MiniNeuralNets.o GradientDescent.o Main.o

################################ Compile ################################

run: gpu_exec

gpu_exec: ${OBJECTS}
	$(NVCC) ${NVCCCFLAGS} -o $@ ${OBJECTS}

Helper.o: Helper.cpp Helper.hpp BasicDataStructures.hpp
	$(NVCC) ${NVCCCFLAGS} -c Helper.cpp

ArffImporter.o: ArffImporter.cpp ArffImporter.hpp Helper.o
	$(NVCC) ${NVCCCFLAGS} -c ArffImporter.cpp

Sigmoid.o: Sigmoid.cpp Sigmoid.hpp Layer.hpp Connection.hpp ActivationFunction.hpp
	$(NVCC) ${NVCCCFLAGS} ${CUFLAGS} -c Sigmoid.cpp

HyperTangent.o: HyperTangent.cpp HyperTangent.hpp Layer.hpp Connection.hpp ActivationFunction.hpp
	$(NVCC) ${NVCCCFLAGS} ${CUFLAGS} -c HyperTangent.cpp

MiniNeuralNets.o: MiniNeuralNets.cpp MiniNeuralNets.hpp Layer.hpp Connection.hpp ActivationFunction.hpp Helper.o
	$(NVCC) ${NVCCCFLAGS} -c MiniNeuralNets.cpp

GradientDescent.o: GradientDescent.cpp GradientDescent.hpp MiniNeuralNets.o
	$(NVCC) ${NVCCCFLAGS} ${CUFLAGS} -c GradientDescent.cpp

Main.o: Main.cpp GradientDescent.o MiniNeuralNets.o Sigmoid.o HyperTangent.o
	$(NVCC) ${NVCCCFLAGS} -c Main.cpp

################################# Clean #################################

clean:
	-rm -f *.o *.h.gch *exec*

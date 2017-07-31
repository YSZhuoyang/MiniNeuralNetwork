
#include "Layer.h"



__global__ void Sigmid(
    float* __restrict__ dOutputMatOffset,
    const unsigned int subMatSize )
{
    unsigned int eleId = blockDim.x * blockIdx.x + threadIdx.x;
    if (eleId >= subMatSize) return;

    float output = dOutputMatOffset[eleId];
    dOutputMatOffset[eleId] = 1.0f / (1.0f + expf(-output));
}

__global__ void ComputeOutputLayerError(
    float* __restrict__ dErrorMat,
    float* __restrict__ dOutputMat,
    const unsigned short* __restrict__ dClassIndexVec,
    const unsigned int errorMatSize )
{
    unsigned int eleId = blockDim.x * blockIdx.x + threadIdx.x;
    if (eleId >= errorMatSize) return;

    float output = dOutputMat[eleId];
    // For testing
    dOutputMat[eleId] = output;
    dErrorMat[eleId] = output - (float) dClassIndexVec[eleId];
}

__global__ void BackPropError(
    float* __restrict__ dPreLayerErrorMat,
    const float* __restrict__ dInputMatOffset,
    const unsigned int preErrorMatSize )
{
    unsigned int eleId = blockDim.x * blockIdx.x + threadIdx.x;
    if (eleId >= preErrorMatSize) return;

    float preError = dInputMatOffset[eleId] * (1.0f - dInputMatOffset[eleId]);
    dPreLayerErrorMat[eleId] *= preError;
}


Layer::Layer()
{

}

Layer::~Layer()
{
    free( weightMat );
    free( outputMat );
    free( errorMat );
    cudaFree( dWeightMat );
    cudaFree( dDeltaWeightMat );
    cudaFree( dOutputMat );
    cudaFree( dErrorMat );
    weightMat = nullptr;
    dDeltaWeightMat = nullptr;
    outputMat = nullptr;
    errorMat = nullptr;
    dWeightMat = nullptr;
    dOutputMat = nullptr;
    dErrorMat = nullptr;
}


void Layer::init(
    const unsigned int numInstances,
    const unsigned int numFeaturesIn,
    const unsigned int numFeaturesOut,
    const unsigned short layerType,
    cublasHandle_t cublasHandle )
{
    if (layerType == OUTPUT_LAYER && numFeaturesOut == 2)
    {
        printf( "Number of classes in output layer can only be 1"
            "for 2 classes or greater than 2 for more than 2 classes\n" );
        return;
    }

    this->cublasHandle = cublasHandle;
    this->numInstances = numInstances;
    this->numFeaturesOut = numFeaturesOut;
    this->numFeaturesIn = numFeaturesIn;
    this->layerType = layerType;
    numNodes = (layerType == OUTPUT_LAYER) ?
        numFeaturesOut : numFeaturesOut - 1;
    weightMatSize = numFeaturesIn * numNodes;
    errorMatSize = numInstances * numNodes;
    outputMatSize = numInstances * numFeaturesOut;
    inputMatSize = numInstances * numFeaturesIn;

    // Allocate host memo
    weightMat = (float*) malloc( weightMatSize * sizeof( float ) );
    outputMat = (float*) malloc( outputMatSize * sizeof( float ) );
    errorMat = (float*) malloc( errorMatSize * sizeof( float ) );

    // Setup bias in non-output layer
    if (layerType == HIDDEN_LAYER)
        // Fill the first feature with X0 for bias
        for (unsigned int i = 0; i < numInstances; i++)
            outputMat[i] = 1.0f;

    // Randomly init weight matrix
    for (unsigned int i = 0; i < weightMatSize; i++)
        weightMat[i] = ((float) (rand() % 101) - 50.0f) / 50.0f;

    /* Determine block and grid size of kernel functions */
    if (outputMatSize > 128)
    {
        ccBlockDim.x = 128;
        ccGridDim.x = (outputMatSize + 127) / 128;
    }
    else ccBlockDim.x = outputMatSize;

    if (errorMatSize > 128)
    {
        sigBlockDim.x = 128;
        sigGridDim.x = (errorMatSize + 127) / 128;
    }
    else sigBlockDim.x = errorMatSize;

    // Allocate device memo
    cudaErrorCheck( cudaMalloc( (void**) &dWeightMat, weightMatSize * sizeof( float ) ) );
    cudaErrorCheck( cudaMalloc( (void**) &dDeltaWeightMat, weightMatSize * sizeof( float ) ) );
    cudaErrorCheck( cudaMalloc( (void**) &dOutputMat, outputMatSize * sizeof( float ) ) );
    cudaErrorCheck( cudaMalloc( (void**) &dErrorMat, errorMatSize * sizeof( float ) ) );
    cudaErrorCheck( cudaMemcpyAsync(
        dWeightMat,
        weightMat,
        weightMatSize * sizeof( float ),
        cudaMemcpyHostToDevice ) );
    // Fill in with X0 as bias
    cudaErrorCheck( cudaMemcpyAsync(
        dOutputMat,
        outputMat,
        numInstances * sizeof( float ),
        cudaMemcpyHostToDevice ) );

    dOutputMatOffset = (layerType != HIDDEN_LAYER) ? dOutputMat : dOutputMat + numInstances;
}

float* Layer::forwardOutput( const float* dInputMat )
{
    // use cublasCgemm3m ...


    const float alpha = 1.0f;
    const float beta = 0.0f;

    cublasErrorCheck( cublasSgemm(
        cublasHandle,
        CUBLAS_OP_N,
        CUBLAS_OP_N,
        numInstances,
        numNodes,
        numFeaturesIn,
        &alpha,
        dInputMat,
        numInstances,
        dWeightMat,
        numFeaturesIn,
        &beta,
        dOutputMatOffset,
        numInstances ) );
    Sigmid<<< sigGridDim, sigBlockDim >>>(
        dOutputMatOffset,
        // Error mat size = output mat size without X0s
        errorMatSize );
    cudaErrorCheck( cudaGetLastError() );

    return dOutputMat;
}

void Layer::backPropError(
    float* dPreLayerErrorMat,
    const float* dInputMat,
    dim3 bpeGridDim,
    dim3 bpeBlockDim )
{
    unsigned int numNodesPreLayer = numFeaturesIn - 1;

    const float alpha = 1.0f;
    const float beta = 0.0f;

    // use cublasCgemm3m ...


    cublasErrorCheck( cublasSgemm(
        cublasHandle,
        CUBLAS_OP_N,
        CUBLAS_OP_T,
        numInstances,
        numNodesPreLayer,
        numFeaturesOut,
        &alpha,
        dErrorMat,
        numInstances,
        // Exclude bias
        dWeightMat + 1,
        numFeaturesIn,
        &beta,
        dPreLayerErrorMat,
        numInstances ) );
    BackPropError<<< bpeGridDim, bpeBlockDim >>>(
        dPreLayerErrorMat,
        dInputMat + numInstances,
        numNodesPreLayer * numInstances );
    cudaErrorCheck( cudaGetLastError() );

    // Copy from device to host
    // For testing gradient descent
    // float* preLayerErrorMat = (float*) malloc( numNodesPreLayer * numInstances * sizeof( float ) );
    // cudaErrorCheck( cudaMemcpy(
    //     preLayerErrorMat,
    //     dPreLayerErrorMat,
    //     numNodesPreLayer * numInstances * sizeof( float ),
    //     cudaMemcpyDeviceToHost ) );

    // float sum = 0.0f;
    // for (unsigned int i = 0; i < numInstances; i++)
    //     for (unsigned int j = 0; j < numNodesPreLayer; j++)
    //         sum += preLayerErrorMat[i * numNodesPreLayer + j];

    // printf( "Err pre: %f\n", sum );
    // free( preLayerErrorMat );
}

void Layer::updateWeights(
    const float* dInputMat,
    const float learningParam )
{
    const float alpha = 1.0f;
    const float beta = 0.0f;

    // Compute delta weight mat
    cublasErrorCheck( cublasSgemm(
        cublasHandle,
        CUBLAS_OP_T,
        CUBLAS_OP_N,
        numFeaturesIn,
        numNodes,
        numInstances,
        &alpha,
        dInputMat,
        numInstances,
        dErrorMat,
        numInstances,
        &beta,
        dDeltaWeightMat,
        numFeaturesIn ) );
    // Update weight mat
    cublasErrorCheck( cublasSaxpy(
        cublasHandle,
        weightMatSize,
        &learningParam,
        dDeltaWeightMat,
        1,
        dWeightMat,
        1 ) );

    // Copy from device to host
    // For testing gradient descent
    cudaErrorCheck( cudaMemcpy(
        weightMat,
        dWeightMat,
        weightMatSize * sizeof( float ),
        cudaMemcpyDeviceToHost ) );

    float sum = 0.0f;
    for (unsigned int i = 0; i < weightMatSize; i++)
        sum += weightMat[i];

    printf( "Back propagate completed, weight sum: %f\n", sum );
}

void Layer::computeOutputLayerError(
    const unsigned short* dClassIndexVec,
    const unsigned short* classIndexVec )
{
    if (layerType != OUTPUT_LAYER)
    {
        printf( "computeOutputLayerError() can only be ran by output layer.\n" );
        return;
    }

    ComputeOutputLayerError<<< ccGridDim, ccBlockDim >>>(
        dErrorMat,
        dOutputMat,
        dClassIndexVec,
        errorMatSize );
    cudaErrorCheck( cudaGetLastError() );

    // Copy from device to host
    // For testing gradient descent
    cudaErrorCheck( cudaMemcpy(
        outputMat,
        dOutputMat,
        outputMatSize * sizeof( float ),
        cudaMemcpyDeviceToHost ) );

    float costSum = 0.0f;
    for (unsigned int i = 0; i < numInstances; i++)
        for (unsigned int j = 0; j < numNodes; j++)
            costSum -= (classIndexVec[i]) ?
                logf(outputMat[i * numNodes + j]) : logf(1.0f - outputMat[i * numNodes + j]);

    printf( "Cost: %f\n", costSum );
}

float* Layer::getDWeightPtr()
{
    return dWeightMat;
}

float* Layer::getDOutputPtr()
{
    return dOutputMat;
}

float* Layer::getDErrorPtr()
{
    return dErrorMat;
}

float* Layer::getWeightPtr()
{
    return weightMat;
}

float* Layer::getOutputPtr()
{
    return outputMat;
}

float* Layer::getErrorPtr()
{
    return errorMat;
}

dim3 Layer::getSigGridDim()
{
    return sigGridDim;
}

dim3 Layer::getSigBlockDim()
{
    return sigBlockDim;
}

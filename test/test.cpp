#include "../src/gpuip.h"
#include <cassert>
#include <stdlib.h>
#include <math.h>
//----------------------------------------------------------------------------//
const char * opencl_codeA = ""
"__kernel void my_kernelA(__global float * A,                               \n"
"                         __global float * B,                               \n"
"                         __global float * C,                               \n"
"                         int incA,                                         \n"
"                         float incB)                                       \n"
"{                                                                          \n"
"    int x = get_global_id(0); int y = get_global_id(1);                    \n"
"    B[x+y*4] =  A[x+y*4] + incA *0.1;                                      \n"
"    C[x+y*4] =  A[x+y*4] + incB;                                           \n"
"}";
const char * opencl_codeB = ""
"__kernel void my_kernelB(__global float * B,                               \n"
"                         __global float * C,                               \n"
"                         __global float * A)                               \n"
"{                                                                          \n"
"    int x = get_global_id(0); int y = get_global_id(1);                    \n"
"    A[x+y*4] =  B[x+y*4] + C[x+y*4];                                       \n"
"}";
//----------------------------------------------------------------------------//
const char * cuda_codeA = ""
"__global__ void my_kernelA(float * A,                                      \n"
"                           float * B,                                      \n"
"                           float * C,                                      \n"
"                           int incA,                                       \n"
"                           float incB)                                     \n"
"{                                                                          \n"
"    if (threadIdx.x < 4 && threadIdx.y < 4) {                              \n"
"        int idx = threadIdx.x + 4 * threadIdx.y;                           \n"
"        B[idx] = A[idx] + incA * 0.1;                                      \n"
"        C[idx] = A[idx] + incB;                                            \n"
"    }"
"}";
const char * cuda_codeB = ""
"__global__ void my_kernelB(float * B,                                      \n"
"                           float * C,                                      \n"
"                           float * A)                                      \n"
"{                                                                          \n"
"    if (threadIdx.x < 4 && threadIdx.y < 4) {                              \n"
"        int idx = threadIdx.x + 4 * threadIdx.y;                           \n"
"        A[idx] = B[idx] + C[idx];                                          \n"
"    }                                                                      \n"
"}";
//----------------------------------------------------------------------------//
const char * glsl_codeA = ""
"uniform sampler2D A;                                                       \n"
"uniform int incA;                                                          \n"
"uniform float incB;                                                        \n"
"varying vec2 texcoord;                                                     \n"
"void main()                                                                \n"
"{                                                                          \n"
"    gl_FragData[0] = vec4(texture2D(A, texcoord).x+incA*0.1,0,0,1);        \n"
"    gl_FragData[1] = vec4(texture2D(A, texcoord).x+incB,0,0,1);            \n"  
"}";
const char * glsl_codeB = ""
"uniform sampler2D B;                                                       \n"
"uniform sampler2D C;                                                       \n"
"varying vec2 texcoord;                                                     \n"
"void main()                                                                \n"
"{                                                                          \n"
"    gl_FragData[0] = vec4(texture2D(B, texcoord).x +                       \n"
"                          texture2D(C, texcoord).x, 0, 0, 1);              \n"
"}";
//----------------------------------------------------------------------------//
inline bool equal(float a, float b)
{
    return fabs(a-b) < 0.001;
}
//----------------------------------------------------------------------------//
void test(gpuip::GpuEnvironment env, const char * codeA, const char * codeB)
{
    const unsigned int width = 4;
    const unsigned int height = 4;
    const unsigned int N = width * height;
    gpuip::Base::Ptr b(gpuip::Base::Create(env));
    b->SetDimensions(width, height);

    gpuip::Buffer b1;
    b1.name = "A";
    b1.channels = 1;
    b1.bpp = sizeof(float);

    gpuip::Buffer b2 = b1;
    b2.name = "B";

    gpuip::Buffer b3 = b1;
    b3.name = "C";
    
    b->AddBuffer(b1);
    b->AddBuffer(b2);
    b->AddBuffer(b3);

    gpuip::Kernel::Ptr kernelA = b->CreateKernel("my_kernelA");
    assert(kernelA.get() != NULL);
    assert(kernelA->name == std::string("my_kernelA"));
    kernelA->code = codeA;
    kernelA->inBuffers.push_back("A");
    kernelA->outBuffers.push_back("B");
    kernelA->outBuffers.push_back("C");

    const gpuip::Parameter<int> incA = { "incA", 2 };
    const gpuip::Parameter<float> incB = { "incB", 0.25};
    kernelA->paramsInt.push_back(incA);
    kernelA->paramsFloat.push_back(incB);

    gpuip::Kernel::Ptr kernelB = b->CreateKernel("my_kernelB");
    assert(kernelB.get() != NULL);
    assert(kernelB->name == std::string("my_kernelB"));
    kernelB->code = codeB;
    kernelB->inBuffers.push_back("B");
    kernelB->inBuffers.push_back("C");
    kernelB->outBuffers.push_back("A");
        
    std::string error;
    b->InitBuffers(&error);
    assert(b->InitBuffers(&error));
    
    std::vector<float> data_in(N);
    for (int i = 0; i < data_in.size(); ++i) {
        data_in[i] = i;
    }
    assert(b->Copy("A", gpuip::Buffer::WRITE_DATA, data_in.data(), &error));
    assert(b->Build(&error));
    assert(b->Process(&error));

    std::vector<float> data_outA(N), data_outB(N), data_outC(N);
    assert(b->Copy("A", gpuip::Buffer::READ_DATA, data_outA.data(), &error));
    assert(b->Copy("B", gpuip::Buffer::READ_DATA, data_outB.data(), &error));
    assert(b->Copy("C", gpuip::Buffer::READ_DATA, data_outC.data(), &error));

    for (int i = 0; i < N; ++i) {
        // Check first kernel call, where B = A + 0.2, C = A + 0.25
        assert(equal(data_outB[i], data_in[i] + incA.value*0.1));
        assert(equal(data_outC[i], data_in[i] + incB.value));

        // Check second kernel call, where A = B + C
        assert(equal(data_outA[i], data_outB[i] + data_outC[i]));
    }
    std::cout << "Test passed!" << std::endl;
}
//----------------------------------------------------------------------------//
int main()
{
    std::cout << "Testing OpenCL..." << std::endl;
    test(gpuip::OpenCL, opencl_codeA, opencl_codeB);
    std::cout << "Testing CUDA..." << std::endl;
    test(gpuip::CUDA, cuda_codeA, cuda_codeB);
    std::cout << "Testing GLSL..." << std::endl;
    test(gpuip::GLSL, glsl_codeA, glsl_codeB);
    return 0;
}
//----------------------------------------------------------------------------//

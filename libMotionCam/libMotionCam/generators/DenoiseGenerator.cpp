#include <Halide.h>
#include <vector>
#include <functional>

using namespace Halide;

using std::vector;
using std::function;

// First stage
const vector<float> F_WAVELET_REAL[] = {
    { 0, -0.08838834764832, 0.08838834764832, 0.695879989034, 0.695879989034, 0.08838834764832, -0.08838834764832,  0.01122679215254,  0.01122679215254, 0 },
    { 0, -0.01122679215254, 0.01122679215254, 0.08838834764832, 0.08838834764832, -0.695879989034, 0.695879989034, -0.08838834764832, -0.08838834764832, 0 }
};

const vector<float> F_WAVELET_IMAG[] = {
    { 0.01122679215254, 0.01122679215254, -0.08838834764832, 0.08838834764832, 0.695879989034, 0.695879989034, 0.08838834764832, -0.08838834764832, 0, 0 },
    { 0, 0, -0.08838834764832, -0.08838834764832, 0.695879989034, -0.695879989034, 0.08838834764832, 0.08838834764832, 0.01122679215254, -0.01122679215254 }
};

// Later stages
const vector<float> WAVELET_REAL[] = {
    { 0.03516384000000, 0, -0.08832942000000, 0.23389032000000, 0.76027237000000, 0.58751830000000, 0, -0.11430184000000, 0, 0 },
    { 0, 0, -0.11430184000000, 0, 0.58751830000000, -0.76027237000000, 0.23389032000000, 0.08832942000000, 0, -0.03516384000000 },
};

const vector<float> WAVELET_IMAG[] = {
    { 0, 0, -0.11430184000000, 0, 0.58751830000000, 0.76027237000000, 0.23389032000000, -0.08832942000000, 0, 0.03516384000000 },
    { -0.03516384000000, 0, 0.08832942000000, 0.23389032000000, -0.76027237000000, 0.58751830000000, 0, -0.11430184000000, 0, 0 },
};

static Func transpose(Func f) {
    // Transpose the first two dimensions of x.
    vector<Var> argsT(f.args());
    std::swap(argsT[0], argsT[1]);

    Func fT(f.name() + "Transposed");
    fT(argsT) = f(f.args());

    return fT;
}

class DenoiseGenerator : public Generator<DenoiseGenerator> {
public:
    Input<Func> input0{"input0", 3};
    Input<Func> input1{"input1", 3};
    Input<Func> pendingOutput{"pendingOutput", 3};

    Input<Buffer<float>> flowMap{"flowMap", 3};

    Input<int32_t> width{"width"};
    Input<int32_t> height{"height"};
    Input<int32_t> whiteLevel{"whiteLevel"};
    
    Input<float> motionVectorsWeight{"motionVectorsWeight"};
    Input<float> differenceWeight{"differenceWeight"};

    Output<Func> output{"output", 3};

    void generate();

private:
    Func blockMean(Func in);
    void cmpSwap(Expr& a, Expr& b);
    Expr median(Expr A, Expr B, Expr C, Expr D);
    Func registeredInput();

    Var v_i{"i"};
    Var v_x{"x"};
    Var v_y{"y"};
    Var v_c{"c"};
    
    Var v_xo{"xo"};
    Var v_xi{"xi"};
    Var v_yo{"yo"};
    Var v_yi{"yi"};

    Var v_xio{"xio"};
    Var v_xii{"xii"};
    Var v_yio{"yio"};
    Var v_yii{"yii"};

    Var subtile_idx{"subtile_idx"};
    Var tile_idx{"tile_idx"};
};

Func DenoiseGenerator::blockMean(Func in) {
    Expr M0 =
        (in(v_x - 1,  v_y - 1,    v_c) + 
         in(v_x,      v_y - 1,    v_c) + 
         in(v_x,      v_y,        v_c) +
         in(v_x - 1,  v_y,        v_c)) / 4;

    Expr M1 =
        (in(v_x,      v_y - 1,    v_c) + 
         in(v_x + 1,  v_y - 1,    v_c) + 
         in(v_x + 1,  v_y,        v_c) +
         in(v_x,      v_y,        v_c)) / 4;

    Expr M2 =
        (in(v_x,      v_y,        v_c) + 
         in(v_x + 1,  v_y,        v_c) + 
         in(v_x,      v_y + 1,    v_c) +
         in(v_x + 1,  v_y + 1,    v_c)) / 4;

    Expr M3 =
        (in(v_x - 1,  v_y,        v_c) + 
         in(v_x,      v_y,        v_c) + 
         in(v_x,      v_y + 1,    v_c) +
         in(v_x - 1,  v_y + 1,    v_c)) / 4;

    Func out;

    out(v_x, v_y, v_c, v_i) =
        select( v_i == 0, M0,
                v_i == 1, M1,
                v_i == 2, M2,
                          M3);

    return out;
}

void DenoiseGenerator::cmpSwap(Expr& a, Expr& b) {
    Expr tmp = min(a, b);
    b = max(a, b);
    a = tmp;
}

Expr DenoiseGenerator::median(Expr A, Expr B, Expr C, Expr D) {
    cmpSwap(A, B);
    cmpSwap(C, D);
    cmpSwap(A, C);
    cmpSwap(B, D);
    cmpSwap(B, C);

    return 0.5f * (B + C);
}

Func DenoiseGenerator::registeredInput() {
    Func result{"registeredInput"};
    Func inputF32{"inputF32"};

    flowMap
        .dim(0).set_stride(2)
        .dim(2).set_stride(1);

    Func clamped = BoundaryConditions::repeat_edge(input1, { {0, width}, {0, height}, {0, 4} } );
    inputF32(v_x, v_y, v_c) = cast<float>(clamped(v_x, v_y, v_c));
    
    Expr flowX = clamp(v_x, 0, flowMap.width() - 1);
    Expr flowY = clamp(v_y, 0, flowMap.height() - 1);
    
    Expr fx = v_x + flowMap(flowX, flowY, 0);
    Expr fy = v_y + flowMap(flowX, flowY, 1);
    
    Expr x = cast<int16_t>(fx);
    Expr y = cast<int16_t>(fy);
    
    Expr a = fx - x;
    Expr b = fy - y;
    
    Expr p0 = lerp(inputF32(x, y, v_c), inputF32(x + 1, y, v_c), a);
    Expr p1 = lerp(inputF32(x, y + 1, v_c), inputF32(x + 1, y + 1, v_c), a);
    
    result(v_x, v_y, v_c) = saturating_cast<uint16_t>(lerp(p0, p1, b) + 0.5f);

    return result;
}

void DenoiseGenerator::generate() {    
    Func inRepeated1 = registeredInput();

    Func inSigned0{"inSigned0"}, inSigned1{"inSigned1"};

    inSigned0(v_x, v_y, v_c) = cast<float>(input0(clamp(v_x, 0, width - 1), clamp(v_y, 0, height - 1), v_c));
    inSigned1(v_x, v_y, v_c) = cast<float>(inRepeated1(v_x, v_y, v_c));

    Func inMean0{"inMean0"}, inMean1{"inMean1"};
    Func inHigh0{"inHigh0"}, inHigh1{"inHigh1"};

    inMean0 = blockMean(inSigned0);
    inMean1 = blockMean(inSigned1);

    inHigh0(v_x, v_y, v_c, v_i) = inSigned0(v_x, v_y, v_c) - inMean0(v_x, v_y, v_c, v_i);
    inHigh1(v_x, v_y, v_c, v_i) = inSigned1(v_x, v_y, v_c) - inMean1(v_x, v_y, v_c, v_i);

    Func T{"T"};

    Expr T0 = median(
        abs(inHigh0(v_x-1,  v_y-1,  v_c, v_i)),
        abs(inHigh0(v_x,    v_y-1,  v_c, v_i)),
        abs(inHigh0(v_x,    v_y,    v_c, v_i)),
        abs(inHigh0(v_x-1,  v_y,    v_c, v_i ))
    );

    Expr T1 = median(
        abs(inHigh0(v_x,    v_y-1,  v_c, v_i)),
        abs(inHigh0(v_x+1,  v_y-1,  v_c, v_i)),
        abs(inHigh0(v_x+1,  v_y,    v_c, v_i)),
        abs(inHigh0(v_x,    v_y,    v_c, v_i))
    );

    Expr T2 = median(
        abs(inHigh0(v_x,    v_y,    v_c, v_i)),
        abs(inHigh0(v_x+1,  v_y,    v_c, v_i)),
        abs(inHigh0(v_x,    v_y+1,  v_c, v_i)),
        abs(inHigh0(v_x+1,  v_y+1,  v_c, v_i))
    );

    Expr T3 = median(
        abs(inHigh0(v_x-1,  v_y,    v_c, v_i)),
        abs(inHigh0(v_x,    v_y,    v_c, v_i)),
        abs(inHigh0(v_x,    v_y+1,  v_c, v_i)),
        abs(inHigh0(v_x-1,  v_y+1,  v_c, v_i))
    );

    T(v_x, v_y, v_c, v_i) =
        select( v_i == 0, T0,
                v_i == 1, T1,
                v_i == 2, T2,
                          T3 );

    Expr D = (abs(inMean0(v_x, v_y, v_c, v_i) - inMean1(v_x, v_y, v_c, v_i))) * (1.0f/cast<float>(whiteLevel));
    Func w{"w"};

    Expr M = flowMap(v_x, v_y, 0)*flowMap(v_x, v_y, 0) + flowMap(v_x, v_y, 1)*flowMap(v_x, v_y, 1);

    w(v_x, v_y, v_c, v_i) = 1.0f + fast_exp(-M/motionVectorsWeight) * differenceWeight*fast_exp(-256.0f * D);

    Func outMean{"outMean"}, outHigh{"outHigh"};

    Expr d0 = inHigh0(v_x, v_y, v_c, v_i) - inHigh1(v_x, v_y, v_c, v_i);
    Expr m0 = abs(d0) / (1e-15f + abs(d0) + w(v_x, v_y, v_c, v_i)*T(v_x, v_y, v_c, v_i));

    outHigh(v_x, v_y, v_c, v_i) = inHigh1(v_x, v_y, v_c, v_i) + m0*d0;

    Expr d1 = inMean0(v_x, v_y, v_c, v_i) - inMean1(v_x, v_y, v_c, v_i);
    Expr m1 = abs(d1) / (1e-15f + abs(d1) + w(v_x, v_y, v_c, v_i)*T(v_x, v_y, v_c, v_i));

    outMean(v_x, v_y, v_c, v_i) = inMean1(v_x, v_y, v_c, v_i) + m1*d1;

    output(v_x, v_y, v_c) = pendingOutput(v_x, v_y, v_c) + 0.25f *
    (
        (outMean(v_x, v_y, v_c, 0) + outHigh(v_x, v_y, v_c, 0)) +
        (outMean(v_x, v_y, v_c, 1) + outHigh(v_x, v_y, v_c, 1)) +
        (outMean(v_x, v_y, v_c, 2) + outHigh(v_x, v_y, v_c, 2)) +
        (outMean(v_x, v_y, v_c, 3) + outHigh(v_x, v_y, v_c, 3))
    );

    input0.set_estimates({{0, 2000}, {0, 1500}, {0, 4}});
    width.set_estimate(2000);
    height.set_estimate(1500);
    whiteLevel.set_estimate(1023);
    motionVectorsWeight.set_estimate(32);
    input1.set_estimates({{0, 2000}, {0, 1500}, {0, 4}});
    pendingOutput.set_estimates({{0, 2000}, {0, 1500}, {0, 4}});
    flowMap.set_estimates({{0, 2000}, {0, 1500}, {0, 4}});

    output.set_estimates({{0, 2000}, {0, 1500}, {0, 4}});
        
    if (!auto_schedule) {
        inSigned0
            .compute_at(output, v_yo)
            .vectorize(v_x, 8);

        inRepeated1
            .compute_root()
            .reorder(v_c, v_x, v_y)
            .bound(v_c, 0, 4)
            .split(v_y, v_yo, v_yi, 32)
            .vectorize(v_x, 8)
            .unroll(v_c)
            .parallel(v_yo);

        output
            .compute_root()
            .reorder(v_x, v_y, v_c)
            .bound(v_c, 0, 4)
            .split(v_y, v_yo, v_yi, 32)
            .vectorize(v_x, 8)
            .parallel(v_yo);
    }
}

class ForwardTransformGenerator : public Generator<ForwardTransformGenerator> {
public:
    GeneratorParam<int> levels{"levels", 6};

    Input<Func> input{"input", 3};
    
    Input<int32_t> width{"width"};
    Input<int32_t> height{"height"};
    Input<int32_t> channel{"channel"};
    
    Output<Func[]> output{"output", 4};

    void generate();
    void schedule();
    void schedule_for_cpu();
    void schedule_for_gpu();
    
    Var v_i{"i"};
    Var v_x{"x"};
    Var v_y{"y"};
    Var v_c{"c"};
    
    Var v_xo{"xo"};
    Var v_xi{"xi"};
    Var v_yo{"yo"};
    Var v_yi{"yi"};

    Var v_xio{"xio"};
    Var v_xii{"xii"};
    Var v_yio{"yio"};
    Var v_yii{"yii"};

    Var subtile_idx{"subtile_idx"};
    Var tile_idx{"tile_idx"};

    //
    
    void forward0(Func& forwardOutput, Func& intermediateOutput, Func in);
    void forward1(Func& forwardOutput, Func& intermediateOutput, Func in);
    
    Expr forwardStep0(Func in, int i, const vector<float>& H);
    Expr forwardStep1(Func in, int c, int i, const vector<float>& H);
    
    Func rawChannel, clamped, inputF32, denoised;

    vector<Func> funcsStage0;
    vector<Func> funcsStage1;
};

Expr ForwardTransformGenerator::forwardStep0(Func in, int i, const vector<float>& H) {
    Expr result = 0.0f;
    
    if(i >= 0) {
        for(int idx = 0; idx < H.size(); idx++) {
            result += in(v_x*2+idx, v_y, i)*H[idx];
        }
    }
    else {
        for(int idx = 0; idx < H.size(); idx++) {
            result += in(v_x*2+idx, v_y)*H[idx];
        }
    }
    
    return result;
}

Expr ForwardTransformGenerator::forwardStep1(Func in, int c, int i, const vector<float>& H) {
    Expr result = 0.0f;
    
    if(i >= 0) {
        for(int idx = 0; idx < H.size(); idx++) {
            result += in(v_x*2+idx, v_y, c, i)*H[idx];
        }
    }
    else {
        for(int idx = 0; idx < H.size(); idx++) {
            result += in(v_x*2+idx, v_y, c)*H[idx];
        }
    }
    
    return result;
}

void ForwardTransformGenerator::forward0(Func& forwardOutput, Func& intermediateOutput, Func image) {
    Expr expr[4];
    
    // Rows
    expr[0] = select(v_c == 0, forwardStep0(image, -1, F_WAVELET_REAL[0]),
                               forwardStep0(image, -1, F_WAVELET_REAL[1]));

    expr[1] = select(v_c == 0, forwardStep0(image, -1, F_WAVELET_IMAG[0]),
                               forwardStep0(image, -1, F_WAVELET_IMAG[1]));

    intermediateOutput(v_x, v_y, v_c, v_i) = select(v_i == 0, expr[0], expr[1]);
    
    // Cols
    Func rowsResultTransposed = transpose(intermediateOutput);

    for(int i = 0; i < 4; i++) {
        int idx = i / 2;
        
        if(i % 2 == 0) {
            expr[i] = select(v_c == 0, forwardStep1(rowsResultTransposed, 0, idx, F_WAVELET_REAL[0]),
                             v_c == 1, forwardStep1(rowsResultTransposed, 0, idx, F_WAVELET_REAL[1]),
                             v_c == 2, forwardStep1(rowsResultTransposed, 1, idx, F_WAVELET_REAL[0]),
                                       forwardStep1(rowsResultTransposed, 1, idx, F_WAVELET_REAL[1]));
        }
        else {
            expr[i] = select(v_c == 0, forwardStep1(rowsResultTransposed, 0, idx, F_WAVELET_IMAG[0]),
                             v_c == 1, forwardStep1(rowsResultTransposed, 0, idx, F_WAVELET_IMAG[1]),
                             v_c == 2, forwardStep1(rowsResultTransposed, 1, idx, F_WAVELET_IMAG[0]),
                                       forwardStep1(rowsResultTransposed, 1, idx, F_WAVELET_IMAG[1]));
            
        }
    }
    
    // Oriented wavelets
    Func forwardTmp;
    
    forwardTmp(v_x, v_y, v_c, v_i) = select(v_i == 0, expr[0],
                                            v_i == 1, expr[1],
                                            v_i == 2, expr[2],
                                                      expr[3]);
    
    forwardOutput(v_x, v_y, v_c, v_i) = select(v_c == 0, forwardTmp(v_x, v_y, v_c, v_i),
                                        select(v_i == 0, (forwardTmp(v_x, v_y, v_c, 0) + forwardTmp(v_x, v_y, v_c, 3)) * sqrt(0.5f),
                                               v_i == 1, (forwardTmp(v_x, v_y, v_c, 1) + forwardTmp(v_x, v_y, v_c, 2)) * sqrt(0.5f),
                                               v_i == 2, (forwardTmp(v_x, v_y, v_c, 1) - forwardTmp(v_x, v_y, v_c, 2)) * sqrt(0.5f),
                                                         (forwardTmp(v_x, v_y, v_c, 0) - forwardTmp(v_x, v_y, v_c, 3)) * sqrt(0.5f)));
}

void ForwardTransformGenerator::forward1(Func& forwardOutput, Func& intermediateOutput, Func image) {
    Expr expr[4];

    // Rows
    for(int i = 0; i < 4; i++) {
        if(i < 2) {
            expr[i] = select(v_c == 0, forwardStep0(image, i, WAVELET_REAL[0]),
                                       forwardStep0(image, i, WAVELET_REAL[1]));
        }
        else {
            expr[i] = select(v_c == 0, forwardStep0(image, i, WAVELET_IMAG[0]),
                                       forwardStep0(image, i, WAVELET_IMAG[1]));
        }
    }
    
    intermediateOutput(v_x, v_y, v_c, v_i) = select(v_i == 0, expr[0],
                                                    v_i == 1, expr[1],
                                                    v_i == 2, expr[2],
                                                              expr[3]);

    // Cols
    Func rowsResultTransposed = transpose(intermediateOutput);
    
    for(int i = 0; i < 4; i++) {
        if(i % 2 == 0) {
            expr[i] = select(v_c == 0, forwardStep1(rowsResultTransposed, 0, i, WAVELET_REAL[0]),
                             v_c == 1, forwardStep1(rowsResultTransposed, 0, i, WAVELET_REAL[1]),
                             v_c == 2, forwardStep1(rowsResultTransposed, 1, i, WAVELET_REAL[0]),
                                       forwardStep1(rowsResultTransposed, 1, i, WAVELET_REAL[1]));
        }
        else {
            expr[i] = select(v_c == 0, forwardStep1(rowsResultTransposed, 0, i, WAVELET_IMAG[0]),
                             v_c == 1, forwardStep1(rowsResultTransposed, 0, i, WAVELET_IMAG[1]),
                             v_c == 2, forwardStep1(rowsResultTransposed, 1, i, WAVELET_IMAG[0]),
                                       forwardStep1(rowsResultTransposed, 1, i, WAVELET_IMAG[1]));
            
        }
    }
    
    // Oriented wavelets
    Func forwardTmp;
    
    forwardTmp(v_x, v_y, v_c, v_i) = select(v_i == 0, expr[0],
                                            v_i == 1, expr[1],
                                            v_i == 2, expr[2],
                                                      expr[3]);
    
    forwardOutput(v_x, v_y, v_c, v_i) = select(v_c == 0, forwardTmp(v_x, v_y, v_c, v_i),
                                        select(v_i == 0, (forwardTmp(v_x, v_y, v_c, 0) + forwardTmp(v_x, v_y, v_c, 3)) * sqrt(0.5f),
                                               v_i == 1, (forwardTmp(v_x, v_y, v_c, 1) + forwardTmp(v_x, v_y, v_c, 2)) * sqrt(0.5f),
                                               v_i == 2, (forwardTmp(v_x, v_y, v_c, 1) - forwardTmp(v_x, v_y, v_c, 2)) * sqrt(0.5f),
                                                         (forwardTmp(v_x, v_y, v_c, 0) - forwardTmp(v_x, v_y, v_c, 3)) * sqrt(0.5f)));
}

void ForwardTransformGenerator::generate() {
    output.resize(levels);
        
    for(int level = 0; level < levels; level++) {
        Func forwardOutput("forwardOutputLvl" + std::to_string(level));
        Func intermediateOutput("intermediateOutputLvl" + std::to_string(level));

        // First level use input image
        if(level == 0) {
            clamped = BoundaryConditions::repeat_image(input, { {0, width}, {0, height} } );
            
            // Select input channel
            rawChannel(v_x, v_y) = clamped(v_x, v_y, channel);

            // Suppress hot pixels
            Expr a0 = rawChannel(v_x - 1, v_y);
            Expr a1 = rawChannel(v_x + 1, v_y);
            Expr a2 = rawChannel(v_x, v_y + 1);
            Expr a3 = rawChannel(v_x, v_y - 1);
            Expr a4 = rawChannel(v_x + 1, v_y + 1);
            Expr a5 = rawChannel(v_x + 1, v_y - 1);
            Expr a6 = rawChannel(v_x - 1, v_y + 1);
            Expr a7 = rawChannel(v_x - 1, v_y - 1);

            Expr threshold = max(a0, a1, a2, a3, a4, a5, a6, a7);

            denoised(v_x, v_y) = clamp(rawChannel(v_x, v_y), 0, threshold);
            inputF32(v_x, v_y) = cast<float>(denoised(v_x, v_y));
            
            forward0(forwardOutput, intermediateOutput, inputF32);
        }
        // Use previous level as input
        else {
            Func in("forwardInLvl" + std::to_string(level));
            Func clampedIn("forwardClampedInLvl" + std::to_string(level));
            
            // Use low pass output from previous level
            in(v_x, v_y, v_i) = output[level - 1](v_x, v_y, 0, v_i);
            
            clampedIn = BoundaryConditions::repeat_image(in, { {0, width >> level}, {0, height >> level} });
            
            forward1(forwardOutput, intermediateOutput, clampedIn);
        }
        
        // Set output of level
        output[level] = transpose(forwardOutput);

        funcsStage0.push_back(intermediateOutput);
        funcsStage1.push_back(forwardOutput);
    }

    if(get_target().has_gpu_feature())
        schedule_for_gpu();
    else
        schedule_for_cpu();
}

void ForwardTransformGenerator::schedule() {
}

void ForwardTransformGenerator::schedule_for_gpu() {
    inputF32
        .reorder(v_x, v_y)
        .compute_at(output[0], tile_idx)
        .gpu_threads(v_x, v_y);

    for(int level = 0; level < levels; level++) {
        if(level < 3) {
            output[level]
                .compute_root()
                .bound(v_i, 0, 4)
                .reorder(v_i, v_x, v_y)
                .tile(v_x, v_y, v_xo, v_yo, v_xi, v_yi, 4, 8)
                .fuse(v_xo, v_yo, tile_idx)
                .tile(v_xi, v_yi, v_xio, v_yio, v_xii, v_yii, 2, 4)
                .fuse(v_xio, v_yio, subtile_idx)
                .unroll(v_i)
                .gpu_blocks(tile_idx)
                .gpu_threads(subtile_idx);

        // Forward
        funcsStage1[level]
            .bound(v_c, 0, 4)
            .reorder(v_c, v_i, v_x, v_y)
            .reorder_storage(v_y, v_x, v_c, v_i)
            .compute_at(output[level], tile_idx)
            .store_at(output[level], tile_idx)
            .unroll(v_c)
            .unroll(v_i)
            .gpu_threads(v_x, v_y);
        
        // Intermediate
        funcsStage0[level]
            .bound(v_c, 0, 4)
            .reorder(v_c, v_i, v_x, v_y)
            .reorder_storage(v_y, v_x, v_c, v_i)
            .compute_at(output[level], tile_idx)
            .store_at(output[level], tile_idx)
            .unroll(v_c)
            .unroll(v_i)
            .gpu_threads(v_x, v_y);
        }
        else {
            output[level]
                .compute_root()
                .bound(v_i, 0, 4)
                .reorder(v_i, v_x, v_y)
                .gpu_tile(v_x, v_y, v_xi, v_yi, 8, 8);

            // Forward
            funcsStage1[level]
                .bound(v_c, 0, 4)
                .reorder(v_c, v_i, v_x, v_y)
                .compute_at(output[level], v_x)
                .gpu_threads(v_x, v_y);
            
            // Intermediate
            funcsStage0[level]
                .bound(v_c, 0, 4)
                .reorder(v_c, v_i, v_x, v_y)
                .compute_at(output[level], v_x)
                .gpu_threads(v_x, v_y);
        }        
    }
}

void ForwardTransformGenerator::schedule_for_cpu() {
    inputF32
        .reorder(v_x, v_y)
        .compute_at(output[0], tile_idx)
        .vectorize(v_x, 4);

    for(int level = 0; level < levels; level++) {
        int outerTileX = 32;
        int outerTileY = 16;

        int innerTileX = 16;
        int innerTileY = 8;
        
        if(level > 3) {
            int outerTileX = 8;
            int outerTileY = 8;

            output[level]
                .compute_root()
                .bound(v_i, 0, 4)
                .reorder(v_i, v_x, v_y)
                .tile(v_x, v_y, v_xo, v_yo, v_xi, v_yi, outerTileX, outerTileY, TailStrategy::GuardWithIf)
                .fuse(v_xo, v_yo, tile_idx)
                .parallel(tile_idx)
                .unroll(v_i)
                .vectorize(v_xi, 4, TailStrategy::GuardWithIf);
            
            // Forward
            funcsStage1[level]
                .bound(v_c, 0, 4)
                .reorder(v_c, v_i, v_y, v_x)
                .reorder_storage(v_y, v_x, v_c, v_i)
                .compute_at(output[level], tile_idx)
                .unroll(v_c)
                .vectorize(v_x, 8, TailStrategy::GuardWithIf);
            
            // Intermediate
            funcsStage0[level]
                .bound(v_c, 0, 4)
                .reorder(v_c, v_i, v_y, v_x)
                .reorder_storage(v_y, v_x, v_c, v_i)
                .compute_at(output[level], tile_idx)
                .unroll(v_c)
                .vectorize(v_x, 8, TailStrategy::GuardWithIf);
        }
        else {        
            output[level]
                .compute_root()
                .bound(v_i, 0, 4)
                .reorder(v_i, v_x, v_y)
                .tile(v_x, v_y, v_xo, v_yo, v_xi, v_yi, outerTileX, outerTileY)
                .fuse(v_xo, v_yo, tile_idx)
                .tile(v_xi, v_yi, v_xio, v_yio, v_xii, v_yii, innerTileX, innerTileY)
                .fuse(v_xio, v_yio, subtile_idx)
                .parallel(tile_idx)
                .unroll(v_i)
                .vectorize(v_xii, 4);
            
            // Forward
            funcsStage1[level]
                .reorder(v_c, v_i, v_y, v_x)
                .reorder_storage(v_y, v_x, v_c, v_i)
                .compute_at(output[level], subtile_idx)
                .store_at(output[level], tile_idx)
                .unroll(v_c)
                .vectorize(v_x, 8);
            
            // Intermediate
            funcsStage0[level]
                .reorder(v_c, v_i, v_y, v_x)
                .reorder_storage(v_y, v_x, v_c, v_i)
                .compute_at(output[level], subtile_idx)
                .store_at(output[level], tile_idx)
                .unroll(v_c)
                .vectorize(v_x, 8);
            }
    }
}

//

class InverseTransformGenerator : public Generator<InverseTransformGenerator> {
public:
    Input<Buffer<float>[]> input{"input", 4};

    Input<float> noiseSigma{"noiseSigma"};
    Input<bool> softThresholding{"softThresholding"};

    Input<int> numFrames{"numFrames", 1};
    Input<float> denoiseAggressiveness{"denoiseAggressiveness", 1.0f};
    
    Output<Buffer<uint16_t>> output{"output", 2};
    
    void generate();
    void schedule();

    //
    
    Var v_i{"i"};
    Var v_x{"x"};
    Var v_y{"y"};
    Var v_c{"c"};
    
    Var v_xo{"xo"};
    Var v_xi{"xi"};
    Var v_yo{"yo"};
    Var v_yi{"yi"};
    
    Var v_xio{"xio"};
    Var v_xii{"xii"};
    Var v_yio{"yio"};
    Var v_yii{"yii"};

    Var subtile_idx{"subtile_idx"};
    Var tile_idx{"tile_idx"};

    //
    
    vector<Func> denoisedOutput;
    vector<Func> inverseOutput;

    void threshold(Expr& outReal, Expr& outImag, Func in, int realIdx, int imagIdx);

    void inverseStep(Expr& out0, Expr& out1, Func in, int idx0, int idx1, int idx, const vector<float>& H0, const vector<float>& H1);
    void inverse(Func& inverseOutput, Func& intermediateOutput, Func wavelet, const vector<float> real[2], const vector<float> imag[2]);
};

void InverseTransformGenerator::inverseStep(Expr& out0, Expr& out1, Func in, int c0, int c1, int i, const vector<float>& H0, const vector<float>& H1) {
    Expr result0 = 0.0f;
    Expr result1 = 0.0f;
    
    int even = (int) H0.size() - 2;
    int odd  = (int) H0.size() - 1;
    
    for(int n = (int) H0.size() / 2 - 1; n >= 0; n--) {
        result0 += in(v_x/2-n, v_y, c0, i)*H0[even] + in(v_x/2-n, v_y, c1, i)*H1[even];
        result1 += in(v_x/2-n, v_y, c0, i)*H0[odd] + in(v_x/2-n, v_y, c1, i)*H1[odd];

        even -= 2;
        odd  -= 2;
    }
    
    out0 = result0;
    out1 = result1;
}

void InverseTransformGenerator::inverse(Func& inverseOutput, Func& intermediateOutput, Func wavelet, const vector<float> real[2], const vector<float> imag[2]) {
    
    // Transpose for cols
    Func waveletTransposed = transpose(wavelet);

    Expr h[2], g[2];

    //
    // Cols
    //
    // Indices for subbands with var c:
    // LL, LH, HL, HH
    // 0   1   2   3
    //
    
    Expr colsExpr[4];

    for(int i = 0; i < 4; i++) {
        if(i % 2 == 0) {
            inverseStep(h[0], h[1], waveletTransposed, 0, 1, i, real[0], real[1]);
            inverseStep(g[0], g[1], waveletTransposed, 2, 3, i, real[0], real[1]);
        }
        else {
            inverseStep(h[0], h[1], waveletTransposed, 0, 1, i, imag[0], imag[1]);
            inverseStep(g[0], g[1], waveletTransposed, 2, 3, i, imag[0], imag[1]);
        }

        colsExpr[i] =  select(v_c == 0, select(v_x % 2 == 0, h[0], h[1]),
                                        select(v_x % 2 == 0, g[0], g[1]));
    }

    intermediateOutput(v_x, v_y, v_c, v_i) = select(v_i == 0, colsExpr[0],
                                                    v_i == 1, colsExpr[1],
                                                    v_i == 2, colsExpr[2],
                                                              colsExpr[3]);
    intermediateOutput
        .bound(v_i, 0, 4)
        .bound(v_c, 0, 4);
    
    // Transpose for rows
    Func colsResultTransposed = transpose(intermediateOutput);

    // Rows
    Expr rowsExpr[4];
    
    for(int i = 0; i < 4; i++) {
        if(i < 2) {
            inverseStep(h[0], h[1], colsResultTransposed, 0, 1, i, real[0], real[1]);
            rowsExpr[i] = select(v_x % 2 == 0, h[0], h[1]);
        }
        else {
            inverseStep(h[0], h[1], colsResultTransposed, 0, 1, i, imag[0], imag[1]);
            rowsExpr[i] = select(v_x % 2 == 0, h[0], h[1]);
        }
    }
    
    inverseOutput(v_x, v_y, v_i) = select(v_i == 0, rowsExpr[0],
                                          v_i == 1, rowsExpr[1],
                                          v_i == 2, rowsExpr[2],
                                                    rowsExpr[3]);
}

void InverseTransformGenerator::threshold(Expr& outReal, Expr& outImag, Func in, int realIdx, int imagIdx) {
    Expr xr = in(v_x, v_y, v_c, realIdx);
    Expr yi = in(v_x, v_y, v_c, imagIdx);
    
    Expr mag = sqrt(xr*xr + yi*yi);

    Expr Y = max(mag - noiseSigma, 0);
    Expr w = mag / (mag + noiseSigma + 1e-5f);

    outReal = select(v_c > 0, select(softThresholding, Y * (xr / mag), w * xr), xr);
    outImag = select(v_c > 0, select(softThresholding, Y * (yi / mag), w * yi), yi);
}

void InverseTransformGenerator::generate() {
    const int levels = (int) input.size();
    const int W = 4656;
    const int H = 3496;
    
    // Threshold coefficients
    for(int level = 0; level < levels; level++) {
        Func denoiseTmp;
        Expr real0, imag0;
        Expr real1, imag1;

        Func spatialDenoise("spatialDenoiseLvl" + std::to_string(level));

        Func in = BoundaryConditions::repeat_image(input.at(level));
        Func thresholded, normalized;

        normalized(v_x, v_y, v_c, v_i) = in(v_x, v_y, v_c, v_i) / max(numFrames - 1.0f, 1.0f);


        threshold(real0, imag0, normalized, 0, 2);
        threshold(real1, imag1, normalized, 1, 3);

        denoiseTmp(v_x, v_y, v_c, v_i) = select(v_i == 0, real0,
                                                v_i == 1, real1,
                                                v_i == 2, imag0,
                                                          imag1);

        // Oriented wavelets
        spatialDenoise(v_x, v_y, v_c, v_i) = select(v_c == 0,  denoiseTmp(v_x, v_y, v_c, v_i),
                                             select(v_i == 0, (denoiseTmp(v_x, v_y, v_c, 0) + denoiseTmp(v_x, v_y, v_c, 3)) * sqrt(0.5f),
                                                    v_i == 1, (denoiseTmp(v_x, v_y, v_c, 1) + denoiseTmp(v_x, v_y, v_c, 2)) * sqrt(0.5f),
                                                    v_i == 2, (denoiseTmp(v_x, v_y, v_c, 1) - denoiseTmp(v_x, v_y, v_c, 2)) * sqrt(0.5f),
                                                              (denoiseTmp(v_x, v_y, v_c, 0) - denoiseTmp(v_x, v_y, v_c, 3)) * sqrt(0.5f)));

        denoisedOutput.push_back(spatialDenoise);
    }

    // Inverse wavelet
    for(int level = levels - 1; level >= 0; level--) {
        int outerTile = 64;
        int innerTileX = 64;
        int innerTileY = 16;
        
        if(level > 3) {
            outerTile = 16;
            innerTileX = 16;
            innerTileY = 8;
        }

        Func inverseInput;
        
        if(level == levels - 1) {
            inverseInput(v_x, v_y, v_c, v_i) = denoisedOutput[level](v_x, v_y, v_c, v_i);
        }
        else {
            // Use output from previous level
            size_t prevOutputIdx = inverseOutput.size() - 1;
            Expr inExpr[4];
            
            for(int idx = 0; idx < 4; idx++) {
                inExpr[idx] = select(v_c == 0, inverseOutput[prevOutputIdx](v_x, v_y, idx),
                                               denoisedOutput[level](v_x, v_y, v_c, idx));
            }
            
            inverseInput(v_x, v_y, v_c, v_i) = select(v_i == 0, inExpr[0],
                                                      v_i == 1, inExpr[1],
                                                      v_i == 2, inExpr[2],
                                                                inExpr[3]);
        }
        
        Func inverseResult("inverseResultLvl" + std::to_string(level));
        Func intermediateResult("intermediateResultLvl" + std::to_string(level));
        
        // Invert wavelets
        if(level == 0) {
            inverse(inverseResult, intermediateResult, inverseInput, F_WAVELET_REAL, F_WAVELET_IMAG);
            
            Func finalResult("finalResult");

            finalResult(v_x, v_y) =
                (inverseResult(v_x, v_y, 0) +
                 inverseResult(v_x, v_y, 1) +
                 inverseResult(v_x, v_y, 2) +
                 inverseResult(v_x, v_y, 3)) / 4.0f;

            output(v_x, v_y) = saturating_cast<uint16_t>(Halide::round(finalResult(v_x, v_y)));
            
            if(get_target().has_gpu_feature()) {
                output
                    .compute_root()
                    .reorder(v_x, v_y)
                    .tile(v_x, v_y, v_xo, v_yo, v_xi, v_yi, 4, 8)
                    .fuse(v_xo, v_yo, tile_idx)
                    .tile(v_xi, v_yi, v_xio, v_yio, v_xii, v_yii, 2, 4)
                    .fuse(v_xio, v_yio, subtile_idx)
                    .gpu_blocks(tile_idx)
                    .gpu_threads(subtile_idx);
                    
                intermediateResult
                    .reorder(v_c, v_i, v_x, v_y)
                    .reorder_storage(v_c, v_i, v_y, v_x)
                    .store_at(output, tile_idx)
                    .compute_at(output, tile_idx)
                    .unroll(v_c)
                    .unroll(v_i)
                    .gpu_threads(v_x, v_y);
                
                denoisedOutput[level]
                    .reorder(v_c, v_i, v_x, v_y)
                    .reorder_storage(v_c, v_i, v_y, v_x)
                    .store_at(output, tile_idx)
                    .compute_at(output, tile_idx)
                    .unroll(v_c)
                    .unroll(v_i)
                    .gpu_threads(v_x, v_y);
            }
            else
            {
                output
                    .compute_root()
                    .reorder(v_x, v_y)
                    .tile(v_x, v_y, v_xo, v_yo, v_xi, v_yi, outerTile, outerTile)
                    .fuse(v_xo, v_yo, tile_idx)
                    .tile(v_xi, v_yi, v_xio, v_yio, v_xii, v_yii, innerTileX, innerTileY)
                    .fuse(v_xio, v_yio, subtile_idx)
                    .vectorize(v_xii, 4)
                    .parallel(tile_idx);
                
                intermediateResult
                    .reorder(v_c, v_i, v_y, v_x)
                    .reorder_storage(v_c, v_i, v_y, v_x)
                    .compute_at(output, subtile_idx)
                    .store_at(output, tile_idx)
                    .vectorize(v_y, 4)
                    .unroll(v_c);
                
                denoisedOutput[level]
                    .reorder(v_c, v_i, v_y, v_x)
                    .reorder_storage(v_c, v_i, v_y, v_x)
                    .compute_at(output, subtile_idx)
                    .store_at(output, tile_idx)
                    .unroll(v_c)
                    .vectorize(v_x, 4);
            }
        }
        else {
            inverse(inverseResult, intermediateResult, inverseInput, WAVELET_REAL, WAVELET_IMAG);
            
            if(get_target().has_gpu_feature()) {
                inverseResult
                    .compute_root()
                    .bound(v_i, 0, 4)
                    .reorder(v_i, v_x, v_y)
                    .tile(v_x, v_y, v_xo, v_yo, v_xi, v_yi, 4, 8)
                    .fuse(v_xo, v_yo, tile_idx)
                    .tile(v_xi, v_yi, v_xio, v_yio, v_xii, v_yii, 2, 4)
                    .fuse(v_xio, v_yio, subtile_idx)
                    .unroll(v_i)
                    .gpu_blocks(tile_idx)
                    .gpu_threads(subtile_idx);

                intermediateResult
                    .reorder(v_c, v_i, v_y, v_x)
                    .reorder_storage(v_c, v_i, v_y, v_x)
                    .compute_at(inverseResult, tile_idx)
                    .store_at(inverseResult, tile_idx)
                    .unroll(v_c)
                    .unroll(v_i)
                    .gpu_threads(v_x, v_y);
                
                denoisedOutput[level]
                    .reorder(v_c, v_i, v_y, v_x)
                    .reorder_storage(v_c, v_i, v_y, v_x)
                    .compute_at(inverseResult, tile_idx)
                    .store_at(inverseResult, tile_idx)
                    .unroll(v_c)
                    .unroll(v_i)
                    .gpu_threads(v_x, v_y);
            }
            else {
                inverseResult
                    .compute_root()
                    .reorder(v_i, v_x, v_y)
                    .tile(v_x, v_y, v_xo, v_yo, v_xi, v_yi, outerTile, outerTile)
                    .fuse(v_xo, v_yo, tile_idx)
                    .tile(v_xi, v_yi, v_xio, v_yio, v_xii, v_yii, innerTileX, innerTileY)
                    .fuse(v_xio, v_yio, subtile_idx)
                    .vectorize(v_xii, 4)
                    .unroll(v_i)
                    .parallel(tile_idx);

                intermediateResult
                    .reorder(v_c, v_i, v_y, v_x)
                    .reorder_storage(v_c, v_i, v_y, v_x)
                    .compute_at(inverseResult, subtile_idx)
                    .store_at(inverseResult, tile_idx)
                    .vectorize(v_y, 4)
                    .unroll(v_c);
                
                denoisedOutput[level]
                    .reorder(v_c, v_i, v_y, v_x)
                    .reorder_storage(v_c, v_i, v_y, v_x)
                    .compute_at(inverseResult, subtile_idx)
                    .store_at(inverseResult, tile_idx)
                    .unroll(v_c)
                    .vectorize(v_x, 4);
            }
        }
        
        inverseOutput.push_back(inverseResult);
    }
}

void InverseTransformGenerator::schedule() {
}

//

class FuseImageGenerator : public Generator<FuseImageGenerator> {
public:
    Input<Func> input{"input", 3};
    Input<int32_t> width{"width"};
    Input<int32_t> height{"height"};
    Input<int32_t> channel{"channel"};
    
    Input<Buffer<float>> flowMap{"flowMap", 3};
    
    Input<Func[]> reference{"reference", 4};
    Input<Func[]> intermediate{"intermediate", 4};

    Input<float> noiseSigma{"noiseSigma"};

    Input<float> denoiseDifferenceWeight{"denoiseDifferenceWeight"};
    Input<float> denoiseWeight{"denoiseWeight"};    

    Input<bool> resetOutput{"resetOutput"};

    Output<Func[]> output{"output", 4};

    void generate();
    void schedule_for_cpu();
    void schedule_for_gpu();
    void schedule();

    void registeredInput(Func& result);

    //
    
    Func registeredImage, inputF32, clamped;
    Func motionWeight;
    vector<Func> fusedLevels;
    std::unique_ptr<ForwardTransformGenerator> forwardTransform;
    
    Var v_i{"i"};
    Var v_x{"x"};
    Var v_y{"y"};
    Var v_c{"c"};
    
    Var v_xo{"xo"};
    Var v_xi{"xi"};
    Var v_yo{"yo"};
    Var v_yi{"yi"};

    Var v_xio{"xio"};
    Var v_xii{"xii"};
    Var v_yio{"yio"};
    Var v_yii{"yii"};

    Var subtile_idx{"subtile_idx"};
    Var tile_idx{"tile_idx"};
};

void FuseImageGenerator::registeredInput(Func& result) {
    clamped = BoundaryConditions::repeat_edge(input, { {0, width}, {0, height} } );
    inputF32(v_x, v_y, v_c) = cast<float>(clamped(v_x, v_y, v_c));
    
    Expr flowX = clamp(v_x, 0, flowMap.width() - 1);
    Expr flowY = clamp(v_y, 0, flowMap.height() - 1);
    
    Expr fx = v_x + flowMap(flowX, flowY, 0);
    Expr fy = v_y + flowMap(flowX, flowY, 1);
    
    Expr x = cast<int>(fx);
    Expr y = cast<int>(fy);
    
    Expr a = fx - x;
    Expr b = fy - y;
    
    Expr p0 = lerp(inputF32(x, y, v_c), inputF32(x + 1, y, v_c), a);
    Expr p1 = lerp(inputF32(x, y + 1, v_c), inputF32(x + 1, y + 1, v_c), a);
    
    result(v_x, v_y, v_c) = saturating_cast<uint16_t>(lerp(p0, p1, b) + 0.5f);
}

void FuseImageGenerator::generate() {
    const int levels = (int) reference.size();
    
    // Flow map is interleaved
    flowMap
        .dim(0).set_stride(2)
        .dim(2).set_stride(1);

    registeredInput(registeredImage);
    
    forwardTransform = create<ForwardTransformGenerator>();

    forwardTransform->levels.set(levels);
    forwardTransform->apply(registeredImage, width, height, channel);
    
    output.resize(levels);

    // Fuse coefficients
    for(int level = 0; level < levels; level++) {
        Expr x = reference.at(level)(v_x, v_y, v_c, v_i);
        Expr y = forwardTransform->output.at(level)(v_x, v_y, v_c, v_i);

        Expr T = noiseSigma;
        Expr d = x - y;

        // Difference in lowpass channel
        Expr D = abs(reference.at(level)(v_x, v_y, 0, 0) - forwardTransform->output.at(level)(v_x, v_y, 0, 0));
        Expr w = max(1.0f, denoiseWeight * exp( -D / denoiseDifferenceWeight));

        Expr m = abs(d) / (abs(d) + w*T + 1e-5f);
        Expr fused = select(v_c > 0, y + m*d, x);

        output[level](v_x, v_y, v_c, v_i) = fused + select(resetOutput, 0.0f, intermediate[level](v_x, v_y, v_c, v_i));
    }

    if(get_target().has_gpu_feature())
        schedule_for_gpu();
    else
        schedule_for_cpu();
}

void FuseImageGenerator::schedule() {
}

void FuseImageGenerator::schedule_for_gpu() {
    const int levels = (int) reference.size();

    registeredImage
        .compute_root()
        .reorder(v_x, v_y)
        .gpu_tile(v_x, v_y, v_xi, v_yi, 8, 16);
    
    for(int level = 0; level < levels; level++) {
        output[level]
            .compute_root()
            .reorder(v_i, v_c, v_x, v_y)
            .bound(v_i, 0, 4)
            .unroll(v_i)
            .gpu_tile(v_x, v_y, v_xi, v_yi, 8, 16);
    }
}

void FuseImageGenerator::schedule_for_cpu() {
    const int levels = (int) reference.size();

    registeredImage
        .compute_root()
        .reorder(v_x, v_y)
        .split(v_y, v_yo, v_yi, 16)
        .vectorize(v_x, 8)
        .parallel(v_yo);
    
    for(int level = 0; level < levels; level++) {
        output[level]
            .compute_root()
            .reorder(v_i, v_c, v_x, v_y)
            .bound(v_i, 0, 4)
            .split(v_y, v_yo, v_yi, 16, TailStrategy::GuardWithIf)
            .parallel(v_yo)
            .unroll(v_i)
            .vectorize(v_x, 8, TailStrategy::GuardWithIf);
    }
}

HALIDE_REGISTER_GENERATOR(DenoiseGenerator, denoise_generator)
HALIDE_REGISTER_GENERATOR(ForwardTransformGenerator, forward_transform_generator)
HALIDE_REGISTER_GENERATOR(FuseImageGenerator, fuse_image_generator)
HALIDE_REGISTER_GENERATOR(InverseTransformGenerator, inverse_transform_generator)

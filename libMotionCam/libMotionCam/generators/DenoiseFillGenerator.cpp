#include <Halide.h>
#include <vector>
#include <functional>

using namespace Halide;

using std::vector;
using std::function;

class DenoiseFillGenerator : public Halide::Generator<DenoiseFillGenerator> {
public:
    Input<int> blackLevel{"blackLevel"};
    Input<int> whiteLevel{"whiteLevel"};
    Input<uint8_t> numOfFrames{"numOfFrames"};
    Input<Buffer<float>> input{"input", 3};
    Output<Func> output{"output", 3};
    Var x, y, c; 

    Var v0_vi{"v0_vi"};
    Var v0_vo{"v0_vo"};
    
    void generate() {
        int EXPANDED_RANGE = 16384;
        Expr p = (input(x, y, c) / numOfFrames) - blackLevel;
        Expr s = EXPANDED_RANGE / cast<float> (whiteLevel-blackLevel);
        output(x, y, c) = cast<uint16_t>(max(0.0f, min(p * s, cast<float> (EXPANDED_RANGE)) ) );

        input.set_estimates({{0, 2000}, {0, 1500}, {0, 4}});
        output.set_estimates({{0, 2000}, {0, 1500}, {0, 4}});

        Var v0 = output.args()[0];
        Var v1 = output.args()[1];
        Var v2 = output.args()[2];
        output
            .compute_root()
            .split(v0, v0_vo, v0_vi, 8)
            .vectorize(v0_vi)
            .parallel(v2)
            .parallel(v1);
    }
};

HALIDE_REGISTER_GENERATOR(DenoiseFillGenerator, denoise_fill_generator)


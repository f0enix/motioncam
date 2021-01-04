#include <Halide.h>

#include <vector>
#include <functional>
#include <limits>

#include "Common.h"

using namespace Halide;
using namespace Halide::ConciseCasts;

using std::vector;
using std::function;
using std::pair;

//
// Guided Image Filtering, by Kaiming He, Jian Sun, and Xiaoou Tang
//

class GuidedFilter : public Halide::Generator<GuidedFilter> {
public:
    GeneratorParam<int> radius{"radius", 31};

    Input<Func> input{"gf_input", 2};
    Output<Func> output{"gf_output", 2};
    
    GeneratorParam<Type> output_type{"output_type", UInt(16)};
    Input<float> eps {"eps"};
    
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

    void generate();

    void schedule();
    void schedule_for_cpu();
    void schedule_for_gpu();
    
    Func I, I2;
    Func mean_I, mean_temp_I;
    Func mean_II, mean_temp_II;
    Func var_I;
    
    Func mean0, mean1, var;
    
    Func a, b;
    Func mean_a, mean_temp_a;
    Func mean_b, mean_temp_b;

private:
    void boxFilter(Func& result, Func& intermediate, Func in);
};

void GuidedFilter::boxFilter(Func& result, Func& intermediate, Func in) {
   const int R = radius;
   RDom r(-R/2, R);

   intermediate(v_x, v_y) = sum(in(v_x + r.x, v_y)) / R;
   result(v_x, v_y) = sum(intermediate(v_x, v_y + r.x)) / R;
        
    // Expr s = 0.0f;
    // Expr t = 0.0f;
    
    // for(int i = -R/2; i <= R/2; i++)
    //     s += in(v_x+i, v_y);
    
    // intermediate(v_x, v_y) = s/R;
    
    // for(int i = -R/2; i <= R/2; i++)
    //     t += intermediate(v_x, v_y+i);
    
    // result(v_x, v_y) = t/R;
}

void GuidedFilter::generate() {    
    I(v_x, v_y) = cast<float>(input(v_x, v_y));    
    I2(v_x, v_y) = I(v_x, v_y) * I(v_x, v_y);
    
    boxFilter(mean_I, mean_temp_I, I);
    boxFilter(mean_II, mean_temp_II, I2);
    
    var_I(v_x, v_y) = mean_II(v_x, v_y) - (mean_I(v_x, v_y) * mean_I(v_x, v_y));
    
    a(v_x, v_y) = var_I(v_x, v_y) / (var_I(v_x, v_y) + eps);
    b(v_x, v_y) = mean_I(v_x, v_y) - (a(v_x, v_y) * mean_I(v_x, v_y));
    
    boxFilter(mean_a, mean_temp_a, a);
    boxFilter(mean_b, mean_temp_b, b);
    
    output(v_x, v_y) = saturating_cast(output_type, (mean_a(v_x, v_y) * I(v_x, v_y)) + mean_b(v_x, v_y));

    if(get_target().has_gpu_feature())
        schedule_for_gpu();
    else
        schedule_for_cpu();
}

void GuidedFilter::schedule() {    
}

void GuidedFilter::schedule_for_cpu() {
   output
        .compute_root()
        .reorder(v_x, v_y)
        .tile(v_x, v_y, v_xo, v_yo, v_xi, v_yi, 128, 128)
        .fuse(v_xo, v_yo, tile_idx)
        .tile(v_xi, v_yi, v_xio, v_yio, v_xii, v_yii, 64, 64)
        .fuse(v_xio, v_yio, subtile_idx)
        .parallel(tile_idx)
        .vectorize(v_xii, 8);
    
    mean_temp_I
        .compute_at(output, subtile_idx)
        .store_at(output, tile_idx)
        .vectorize(v_x, 8);

    mean_temp_II
        .compute_at(output, subtile_idx)
        .store_at(output, tile_idx)
        .vectorize(v_x, 8);

    var_I
        .compute_at(output, subtile_idx)
        .store_at(output, tile_idx)
        .vectorize(v_x, 8);

    mean_I
        .compute_at(output, subtile_idx)
        .store_at(output, tile_idx)
        .vectorize(v_x, 8);
    
    mean_II
        .compute_at(output, subtile_idx)
        .store_at(output, tile_idx)
        .vectorize(v_x, 8);

    a
        .compute_at(output, subtile_idx)
        .store_at(output, tile_idx)
        .vectorize(v_x, 8);

    b
        .compute_at(output, subtile_idx)
        .store_at(output, tile_idx)
        .vectorize(v_x, 8);

    mean_temp_a
        .compute_at(output, subtile_idx)
        .store_at(output, tile_idx)
        .vectorize(v_x, 8);

    mean_temp_b
        .compute_at(output, subtile_idx)
        .store_at(output, tile_idx)
        .vectorize(v_x, 8);

    mean_a
        .compute_at(output, subtile_idx)
        .store_at(output, tile_idx)
        .vectorize(v_x, 8);

    mean_b
        .compute_at(output, subtile_idx)
        .store_at(output, tile_idx)
        .vectorize(v_x, 8);    
}

void GuidedFilter::schedule_for_gpu() {
    output
        .compute_root()
        .reorder(v_x, v_y)
        .gpu_tile(v_x, v_y, v_xi, v_yi, 16, 32);
    
    mean_temp_I
        .compute_at(mean_I, v_x)
        .gpu_threads(v_x, v_y);

    mean_temp_II
        .compute_at(mean_II, v_x)
        .gpu_threads(v_x, v_y);


    mean_I
        .compute_root()
        .reorder(v_x, v_y)
        .gpu_tile(v_x, v_y, v_xi, v_yi, 16, 32);
    
    mean_II
        .compute_root()
        .reorder(v_x, v_y)
        .gpu_tile(v_x, v_y, v_xi, v_yi, 16, 32);

    mean_temp_a
        .compute_at(mean_a, v_x)
        .gpu_threads(v_x, v_y);

    mean_temp_b
        .compute_at(mean_b, v_x)
        .gpu_threads(v_x, v_y);

    mean_a
        .compute_root()
        .reorder(v_x, v_y)
        .gpu_tile(v_x, v_y, v_xi, v_yi, 16, 32);

    mean_b
        .compute_root()
        .reorder(v_x, v_y)
        .gpu_tile(v_x, v_y, v_xi, v_yi, 16, 32);    
}
//

//
// Color filter array demosaicking: new method and performance measures
// W Lu, YP Tan - IEEE transactions on image processing, 2003 - ieeexplore.ieee.org
//
// Directional LMMSE Image Demosaicking, Image Processing On Line, 1 (2011), pp. 117–126.
// Pascal Getreuer, Zhang-Wu
//

class Demosaic : public Halide::Generator<Demosaic> {
public:
    // Inputs and outputs
    Input<Func> bayerInput{"bayerInput", Int(16), 2 };

    Input<int> width{"width"};
    Input<int> height{"height"};
    Input<int> sensorArrangement{"sensorArrangement"};

    Output<Func> output{ "demosaicOutput", UInt(16), 3 };

    //
    
    Var v_x{"x"};
    Var v_y{"y"};
    Var v_c{"c"};
    
    Var v_xo{"v_xo"};
    Var v_yo{"v_yo"};
    Var v_xi{"v_xi"};
    Var v_yi{"v_yi"};
    Var v_xio{"v_xio"};
    Var v_xii{"v_xii"};
    
    Func redIntermediate{"red"};
    Func red{"red"};
    Func green{"green"};
    Func blueIntermediate{"blue"};
    Func blue{"blue"};

    void generate();
    void schedule();
    
    void cmpSwap(Expr& a, Expr& b);

    void medianFilter(Func& output, Func input);

    void calculateGreen(Func& output, Func input);
    void calculateGreen2(Func& output, Func input);

    void calculateRed(Func& output, Func input, Func green);
    void calculateBlue(Func& output, Func input, Func green);
};

void Demosaic::medianFilter(Func& output, Func input) {
    Expr p0 = input(v_x,   v_y);
    Expr p1 = input(v_x,   v_y);
    Expr p2 = input(v_x,   v_y);
    Expr p3 = input(v_x,   v_y);

    Expr p4 = input(v_x-1, v_y);
    Expr p5 = input(v_x-1, v_y);

    Expr p6 = input(v_x+1, v_y);
    Expr p7 = input(v_x+1, v_y);

    Expr p8 = input(v_x,   v_y-1);
    Expr p9 = input(v_x,   v_y-1);

    Expr p10 = input(v_x,  v_y+1);
    Expr p11 = input(v_x,  v_y+1);

    Expr p12 = input(v_x-2, v_y);
    Expr p13 = input(v_x+2, v_y);

    Expr p14 = input(v_x,   v_y-2);
    Expr p15 = input(v_x,   v_y+2);

    Expr p16 = input(v_x-1, v_y-1);
    Expr p17 = input(v_x-1, v_y+1);

    Expr p18 = input(v_x+1, v_y-1);
    Expr p19 = input(v_x+1, v_y+1);

    cmpSwap(p0, p1);
    cmpSwap(p3, p4);
    cmpSwap(p2, p4);
    cmpSwap(p2, p3);
    cmpSwap(p0, p3);
    cmpSwap(p0, p2);
    cmpSwap(p1, p4);
    cmpSwap(p1, p3);
    cmpSwap(p1, p2);
    cmpSwap(p5, p6);
    cmpSwap(p8, p9);
    cmpSwap(p7, p9);
    cmpSwap(p7, p8);
    cmpSwap(p5, p8);
    cmpSwap(p5, p7);
    cmpSwap(p6, p9);
    cmpSwap(p6, p8);
    cmpSwap(p6, p7);
    cmpSwap(p0, p5);
    cmpSwap(p1, p6);
    cmpSwap(p1, p5);
    cmpSwap(p2, p7);
    cmpSwap(p3, p8);
    cmpSwap(p4, p9);
    cmpSwap(p4, p8);
    cmpSwap(p3, p7);
    cmpSwap(p4, p7);
    cmpSwap(p2, p5);
    cmpSwap(p3, p6);
    cmpSwap(p4, p6);
    cmpSwap(p3, p5);
    cmpSwap(p4, p5);
    cmpSwap(p10, p11);
    cmpSwap(p13, p14);
    cmpSwap(p12, p14);
    cmpSwap(p12, p13);
    cmpSwap(p10, p13);
    cmpSwap(p10, p12);
    cmpSwap(p11, p14);
    cmpSwap(p11, p13);
    cmpSwap(p11, p12);
    cmpSwap(p15, p16);
    cmpSwap(p18, p19);
    cmpSwap(p17, p19);
    cmpSwap(p17, p18);
    cmpSwap(p15, p18);
    cmpSwap(p15, p17);
    cmpSwap(p16, p19);
    cmpSwap(p16, p18);
    cmpSwap(p16, p17);
    cmpSwap(p10, p15);
    cmpSwap(p11, p16);
    cmpSwap(p11, p15);
    cmpSwap(p12, p17);
    cmpSwap(p13, p18);
    cmpSwap(p14, p19);
    cmpSwap(p14, p18);
    cmpSwap(p13, p17);
    cmpSwap(p14, p17);
    cmpSwap(p12, p15);
    cmpSwap(p13, p16);
    cmpSwap(p14, p16);
    cmpSwap(p13, p15);
    cmpSwap(p14, p15);
    cmpSwap(p0, p10);
    cmpSwap(p1, p11);
    cmpSwap(p1, p10);
    cmpSwap(p2, p12);
    cmpSwap(p3, p13);
    cmpSwap(p4, p14);
    cmpSwap(p4, p13);
    cmpSwap(p3, p12);
    cmpSwap(p4, p12);
    cmpSwap(p2, p10);
    cmpSwap(p3, p11);
    cmpSwap(p4, p11);
    cmpSwap(p3, p10);
    cmpSwap(p4, p10);
    cmpSwap(p5, p15);
    cmpSwap(p6, p16);
    cmpSwap(p6, p15);
    cmpSwap(p7, p17);
    cmpSwap(p8, p18);
    cmpSwap(p9, p19);
    cmpSwap(p9, p18);
    cmpSwap(p8, p17);
    cmpSwap(p9, p17);
    cmpSwap(p7, p15);
    cmpSwap(p8, p16);
    cmpSwap(p9, p16);
    cmpSwap(p8, p15);
    cmpSwap(p9, p15);
    cmpSwap(p5, p10);
    cmpSwap(p6, p11);
    cmpSwap(p6, p10);
    cmpSwap(p7, p12);
    cmpSwap(p8, p13);
    cmpSwap(p9, p14);
    cmpSwap(p9, p13);
    cmpSwap(p8, p12);
    cmpSwap(p9, p12);
    cmpSwap(p7, p10);
    cmpSwap(p8, p11);
    cmpSwap(p9, p11);
    cmpSwap(p8, p10);
    cmpSwap(p9, p10);

    output(v_x, v_y) = cast<int16_t>((cast<int32_t>(p9) + cast<int32_t>(p10)) / 2);
}

void Demosaic::cmpSwap(Expr& a, Expr& b) {
    Expr tmp = min(a, b);
    b = max(a, b);
    a = tmp;
}

void Demosaic::calculateGreen2(Func& output, Func input) {
    const int M = 4;
    const float DivEpsilon = 0.1f/(1024.0f*1024.0f);

    Func filteredH, filteredV, diffH, diffV, smoothedH, smoothedV;    

    filteredH(v_x, v_y) = -0.25f*input(v_x-2, v_y) + 0.5f*input(v_x-1, v_y) + 0.5f*input(v_x, v_y) + 0.5f*input(v_x+1, v_y) - 0.25f*input(v_x+2, v_y);
    filteredV(v_x, v_y) = -0.25f*input(v_x, v_y-2) + 0.5f*input(v_x, v_y-1) + 0.5f*input(v_x, v_y) + 0.5f*input(v_x, v_y+1) - 0.25f*input(v_x, v_y+2);

    diffH(v_x, v_y) =
        select(((v_x + v_y) & 1) == 1,  input(v_x, v_y) - filteredH(v_x, v_y),
                                        filteredH(v_x, v_y) - input(v_x, v_y));

    diffV(v_x, v_y) =
        select(((v_x + v_y) & 1) == 1,  input(v_x, v_y) - filteredV(v_x, v_y),
                                        filteredV(v_x, v_y) - input(v_x, v_y));

    smoothedH(v_x, v_y) = 0.0312500f*diffH(v_x-4, v_y) + 0.0703125f*diffH(v_x-3, v_y) + 0.1171875f*diffH(v_x-2, v_y) +
                          0.1796875f*diffH(v_x-1, v_y) + 0.2031250f*diffH(v_x,   v_y) + 0.1796875f*diffH(v_x+1, v_y) +
                          0.1171875f*diffH(v_x+2, v_y) + 0.0703125f*diffH(v_x+3, v_y) + 0.0312500f*diffH(v_x+4, v_y);

    smoothedV(v_x, v_y) = 0.0312500f*diffV(v_x, v_y-4) + 0.0703125f*diffV(v_x, v_y-3) + 0.1171875f*diffV(v_x, v_y-2) +
                          0.1796875f*diffV(v_x, v_y-1) + 0.2031250f*diffV(v_x, v_y)   + 0.1796875f*diffV(v_x, v_y+1) +
                          0.1171875f*diffV(v_x, v_y+2) + 0.0703125f*diffV(v_x, v_y+3) + 0.0312500f*diffV(v_x, v_y+4);

    Expr momh1, ph, rh;
    Expr momv1, pv, rv;

    momh1 = ph = rh = 0;
    momv1 = pv = rv = 0;

    Expr mh = smoothedH(v_x, v_y);
    Expr mv = smoothedV(v_x, v_y);

    for(int m = -M; m <= M; m++) {
        momh1 += smoothedH(v_x+m, v_y);
        ph += smoothedH(v_x+m, v_y) * smoothedH(v_x+m, v_y);
        rh += (smoothedH(v_x+m, v_y) - diffH(v_x+m, v_y)) * (smoothedH(v_x+m, v_y) - diffH(v_x+m, v_y));

        momv1 += smoothedV(v_x+m, v_y);
        pv += smoothedV(v_x+m, v_y) * smoothedV(v_x+m, v_y);
        rv += (smoothedV(v_x+m, v_y) - diffV(v_x+m, v_y)) * (smoothedV(v_x+m, v_y) - diffV(v_x+m, v_y));
    }

    Expr Ph = ph / (2*M) - momh1*momh1 / (2*M*(2*M + 1));
    Expr Rh = rh / (2*M + 1) + DivEpsilon;
    Expr h = mh + (Ph / (Ph + Rh)) * (diffH(v_x, v_y) - mh);
    Expr H = Ph - (Ph / (Ph + Rh)) * Ph + DivEpsilon;

    Expr Pv = pv / (2*M) - momv1*momv1 / (2*M*(2*M + 1));
    Expr Rv = rv / (2*M + 1) + DivEpsilon;
    Expr v = mv + (Pv / (Pv + Rv)) * (diffV(v_x, v_y) - mv);
    Expr V = Pv - (Pv / (Pv + Rv)) * Pv + DivEpsilon;

    Expr interp = input(v_x, v_y) + (V*h + H*v) / (H + V);

    output(v_x, v_y) = select(
        ((v_x + v_y) & 1) == 1,
            input(v_x, v_y),
            saturating_cast<int16_t>(interp + 0.5f));
}

void Demosaic::calculateGreen(Func& output, Func input) {
    // Estimate green channel first
    Expr g14 = input(v_x + 0, v_y - 3);
    Expr g23 = input(v_x - 1, v_y - 2);
    Expr g25 = input(v_x + 1, v_y - 2);
    Expr g32 = input(v_x - 2, v_y - 1);
    Expr g34 = input(v_x + 0, v_y - 1);
    Expr g36 = input(v_x + 2, v_y - 1);
    Expr g41 = input(v_x - 3, v_y + 0);
    Expr g43 = input(v_x - 1, v_y + 0);
    
    Expr g45 = input(v_x + 1, v_y + 0);
    Expr g47 = input(v_x + 3, v_y + 0);
    Expr g52 = input(v_x - 2, v_y + 1);
    Expr g54 = input(v_x + 0, v_y + 1);
    Expr g56 = input(v_x + 2, v_y + 1);
    Expr g63 = input(v_x - 1, v_y + 2);
    Expr g65 = input(v_x + 1, v_y + 2);
    Expr g74 = input(v_x + 0, v_y + 3);
    
    Expr b24 = input(v_x + 0, v_y - 2);
    Expr b42 = input(v_x - 2, v_y + 0);
    Expr b44 = input(v_x + 0, v_y + 0);
    Expr b46 = input(v_x + 2, v_y + 0);
    Expr b64 = input(v_x + 0, v_y + 2);
    
    Expr w0 = 1.0f / (1.0f + abs(g54 - g34) + abs(g34 - g14) + abs(b44 - b24) + abs((g43 - g23) / 2) + abs((g45 - g25) / 2));
    Expr w1 = 1.0f / (1.0f + abs(g45 - g43) + abs(g43 - g41) + abs(b44 - b42) + abs((g34 - g32) / 2) + abs((g54 - g52) / 2));
    Expr w2 = 1.0f / (1.0f + abs(g43 - g45) + abs(g45 - g47) + abs(b44 - b46) + abs((g34 - g36) / 2) + abs((g54 - g56) / 2));
    Expr w3 = 1.0f / (1.0f + abs(g34 - g54) + abs(g54 - g74) + abs(b44 - b64) + abs((g43 - g63) / 2) + abs((g45 - g65) / 2));
    
    Expr g0 = g34 + (b44 - b24) / 2;
    Expr g1 = g43 + (b44 - b42) / 2;
    Expr g2 = g45 + (b44 - b46) / 2;
    Expr g3 = g54 + (b44 - b64) / 2;
    
    Expr interp = (w0*g0 + w1*g1 + w2*g2 + w3*g3) / (w0 + w1 + w2 + w3);
    
    output(v_x, v_y) = select(((v_x + v_y) & 1) == 1, cast<int16_t>(input(v_x, v_y)), cast<int16_t>(interp));
}

void Demosaic::calculateRed(Func& output, Func input, Func green) {
    Func I, blurX, blurY, result;

    I(v_x, v_y) = cast<int32_t>(select(v_y % 2 == 0,  select(v_x % 2 == 0, input(v_x, v_y) - green(v_x, v_y), 0),
                                                      0));
    blurX(v_x, v_y) = (
        1 * I(v_x - 1, v_y) +
        2 * I(v_x    , v_y) +
        1 * I(v_x + 1, v_y)
    );

    redIntermediate(v_x, v_y) =
        cast<int16_t> (
            (
              1 * blurX(v_x, v_y - 1) +
              2 * blurX(v_x, v_y)     +
              1 * blurX(v_x, v_y + 1)             
            ) / 4
    );

    Func filtered;
    medianFilter(filtered, redIntermediate);

    output(v_x, v_y) = green(v_x, v_y) + filtered(v_x, v_y);
}

void Demosaic::calculateBlue(Func& output, Func input, Func green) {
    Func I, blurX, blurY, result;

    I(v_x, v_y) = cast<int32_t>(
        select(v_y % 2 == 0, 0,
                             select(v_x % 2 == 0, 0, input(v_x, v_y) - green(v_x, v_y))));

    blurX(v_x, v_y) = (
        1 * I(v_x - 1, v_y) +
        2 * I(v_x    , v_y) +
        1 * I(v_x + 1, v_y)
    );

    blueIntermediate(v_x, v_y) =
        cast<int16_t> (
            (
              1 * blurX(v_x, v_y - 1) +
              2 * blurX(v_x, v_y)     +
              1 * blurX(v_x, v_y + 1)             
            ) / 4
    );

    Func filtered;
    medianFilter(filtered, blueIntermediate);

    output(v_x, v_y) = green(v_x, v_y) + filtered(v_x, v_y);        
}

void Demosaic::generate() {

    calculateGreen(green, bayerInput);
    calculateRed(red, bayerInput, green);
    calculateBlue(blue, bayerInput, green);

    output(v_x, v_y, v_c) = select(v_c == 0, red(v_x, v_y),
                                   v_c == 1, green(v_x, v_y),
                                             blue(v_x, v_y));
}

void Demosaic::schedule() {    
}

//

class TonemapGenerator : public Halide::Generator<TonemapGenerator> {
public:
    // Inputs and outputs
    GeneratorParam<int> tonemap_levels {"tonemap_levels", 9};
    GeneratorParam<Type> output_type{"output_type", UInt(16)};

    Input<Func> input{"input", 3 };
    Output<Func> output{ "tonemapOutput", 3 };

    Input<int> width {"width"};
    Input<int> height {"height"};

    Input<float> variance {"variance"};
    Input<float> gamma {"gamma"};
    Input<float> gain {"gain"};
    
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

    void pyramidUp(Func& output, Func& intermediate, Func input);
    void pyramidDown(Func& output, Func& intermediate, Func input);
    
    vector<pair<Func, Func>> buildPyramid(Func input, int maxlevel);

    void generate();
    void schedule();

    vector<pair<Func, Func>> tonemapPyramid;
    vector<pair<Func, Func>> weightsPyramid;
};

void TonemapGenerator::pyramidUp(Func& output, Func& intermediate, Func input) {
    Func blurX("blurX");
    Func blurY("blurY");

    // Insert zeros and expand by factor of 2 in both dims
    Func expandedX;
    Func expanded("expanded");

    expandedX(v_x, v_y, v_c) = select((v_x % 2)==0, input(v_x/2, v_y, v_c), 0);
    expanded(v_x, v_y, v_c)  = select((v_y % 2)==0, expandedX(v_x, v_y/2, v_c), 0);

    blurX(v_x, v_y, v_c) = fast_integer_divide(
         (1 * expanded(v_x - 2, v_y, v_c) +
          4 * expanded(v_x - 1, v_y, v_c) +
          6 * expanded(v_x,     v_y, v_c) +
          4 * expanded(v_x + 1, v_y, v_c) +
          1 * expanded(v_x + 2, v_y, v_c)
          ), 16);

    blurY(v_x, v_y, v_c) = fast_integer_divide(
         (1 * blurX(v_x, v_y - 2, v_c) +
          4 * blurX(v_x, v_y - 1, v_c) +
          6 * blurX(v_x, v_y,     v_c) +
          4 * blurX(v_x, v_y + 1, v_c) +
          1 * blurX(v_x, v_y + 2, v_c)
          ), 16);

    intermediate = blurX;
    output(v_x, v_y, v_c) = 4 * blurY(v_x, v_y, v_c);
}

void TonemapGenerator::pyramidDown(Func& output, Func& intermediate, Func input) {
    Func blurX, blurY;

    blurX(v_x, v_y, v_c) = fast_integer_divide(
         (1 * input(v_x - 2, v_y, v_c) +
          4 * input(v_x - 1, v_y, v_c) +
          6 * input(v_x,     v_y, v_c) +
          4 * input(v_x + 1, v_y, v_c) +
          1 * input(v_x + 2, v_y, v_c)
          ), 16);

    blurY(v_x, v_y, v_c) = fast_integer_divide(
         (1 * blurX(v_x, v_y - 2, v_c) +
          4 * blurX(v_x, v_y - 1, v_c) +
          6 * blurX(v_x, v_y,     v_c) +
          4 * blurX(v_x, v_y + 1, v_c) +
          1 * blurX(v_x, v_y + 2, v_c)
          ), 16);

    intermediate = blurX;
    output(v_x, v_y, v_c) = blurY(v_x * 2, v_y * 2, v_c);
}

vector<pair<Func, Func>> TonemapGenerator::buildPyramid(Func input, int maxlevel) {
    vector<pair<Func, Func>> pyramid;

    for(int level = 1; level <= maxlevel; level++) {
        Func pyramidDownOutput("pyramidDownLvl" + std::to_string(level));
        Func pyramidDownIntermediate("pyramidDownIntermediateLvl" + std::to_string(level));
        
        Func inClamped;

        if(level == 1) {
            inClamped = BoundaryConditions::repeat_edge(input, { {0, width}, {0, height} } );

            pyramid.push_back(std::make_pair(inClamped, inClamped));
        }
        else {
            inClamped = BoundaryConditions::repeat_edge(pyramid[level - 1].second, { {0, width >> (level-1)}, {0, height >> (level-1)} } );
        }

        pyramidDown(pyramidDownOutput, pyramidDownIntermediate, inClamped);
        
        pyramid.push_back(std::make_pair(pyramidDownIntermediate, pyramidDownOutput));
    }

    return pyramid;
}

void TonemapGenerator::generate() {
    Func gammaLut, inverseGammaLut;

    Expr type_max = ((Type)output_type).max();

    gammaLut(v_i) = cast(output_type, pow(v_i / cast<float>(type_max), 1.0f / gamma) * type_max);
    inverseGammaLut(v_i) = cast(output_type, pow(v_i / cast<float>(type_max), gamma) * type_max);
    
    if(get_target().has_gpu_feature()) {
        gammaLut.compute_root().gpu_tile(v_i, v_xi, 16);
        inverseGammaLut.compute_root().gpu_tile(v_i, v_xi, 16);
    }
    else {
        gammaLut.compute_root().vectorize(v_i, 8);
        inverseGammaLut.compute_root().vectorize(v_i, 8);
    }
    
    // Create two exposures
    Func exposures, weightsLut, weights, weightsNormalized;
    
    Expr ia = input(v_x, v_y, 0);
    Expr ib = cast(output_type, clamp(cast<float>(input(v_x, v_y, 0)) * gain, 0.0f, type_max));

    exposures(v_x, v_y, v_c) = select(v_c == 0, cast<int32_t>(gammaLut(ia)),
                                                cast<int32_t>(gammaLut(ib)));

    // Create weights LUT based on well exposed pixels
    Expr wa = v_i / cast<float>(type_max) - 0.5f;
    Expr wb = -pow(wa, 2) / (2 * variance * variance);
    
    weightsLut(v_i) = cast<int16_t>(clamp(exp(wb) * 32767, -32767, 32767));
    
    if(get_target().has_gpu_feature()) {
        weightsLut.compute_root().gpu_tile(v_i, v_xi, 16);
    }
    else {
        weightsLut.compute_root().vectorize(v_i, 8);
    }

    weights(v_x, v_y, v_c) = weightsLut(cast<uint16_t>(exposures(v_x, v_y, v_c))) / 32767.0f;
    weightsNormalized(v_x, v_y, v_c) = weights(v_x, v_y, v_c) / (1e-12f + weights(v_x, v_y, 0) + weights(v_x, v_y, 1));

    // Create pyramid input
    tonemapPyramid = buildPyramid(exposures, tonemap_levels);
    weightsPyramid = buildPyramid(weightsNormalized, tonemap_levels);

    if(get_target().has_gpu_feature()) {
        tonemapPyramid[0].first.in(tonemapPyramid[1].first)
            .compute_at(tonemapPyramid[1].second, v_x)
            .reorder(v_c, v_x, v_y)
            .unroll(v_c)
            .gpu_threads(v_x, v_y);
        
        weightsPyramid[0].first.in(weightsPyramid[1].first)
            .compute_at(weightsPyramid[1].second, v_x)
            .reorder(v_c, v_x, v_y)
            .unroll(v_c)
            .gpu_threads(v_x, v_y);
    }
    else {
        tonemapPyramid[0].first.in(tonemapPyramid[1].first)
            .compute_at(tonemapPyramid[1].second, v_yi)
            .reorder(v_c, v_x, v_y)
            .unroll(v_c);
        
        weightsPyramid[0].first.in(weightsPyramid[1].first)
            .compute_at(weightsPyramid[1].second, v_yi)
            .reorder(v_c, v_x, v_y)
            .unroll(v_c);
    }

    if(get_target().has_gpu_feature()) {
        for(int level = 1; level < tonemap_levels; level++) {
            tonemapPyramid[level].first
                .reorder(v_c, v_x, v_y)
                .compute_at(tonemapPyramid[level].second, v_x)
                .unroll(v_c)
                .gpu_threads(v_x, v_y);

            tonemapPyramid[level].second                
                .compute_root()
                .reorder(v_c, v_x, v_y)
                .unroll(v_c)
                .gpu_tile(v_x, v_y, v_xi, v_yi, 8, 8);

            weightsPyramid[level].first
                .reorder(v_c, v_x, v_y)
                .compute_at(weightsPyramid[level].second, v_x)
                .unroll(v_c)
                .gpu_threads(v_x, v_y);

            weightsPyramid[level].second
                .compute_root()
                .reorder(v_c, v_x, v_y)
                .unroll(v_c)
                .gpu_tile(v_x, v_y, v_xi, v_yi, 8, 8);
        }
    }
    else {
        for(int level = 1; level < tonemap_levels; level++) {
            tonemapPyramid[level].first
                .compute_at(tonemapPyramid[level].second, v_yi)
                .store_at(tonemapPyramid[level].second, v_yo)
                .vectorize(v_x, 4);

            tonemapPyramid[level].second
                .compute_root()
                .reorder(v_x, v_y)
                .split(v_y, v_yo, v_yi, 64)
                .vectorize(v_x, 4)
                .parallel(v_yo);
        }

        for(int level = 1; level < tonemap_levels; level++) {
            weightsPyramid[level].first
                .compute_at(weightsPyramid[level].second, v_yi)
                .store_at(weightsPyramid[level].second, v_yo)
                .unroll(v_c)
                .vectorize(v_x, 4);

            weightsPyramid[level].second
                .compute_root()
                .reorder(v_c, v_x, v_y)
                .unroll(v_c)
                .split(v_y, v_yo, v_yi, 64)
                .vectorize(v_x, 4)
                .parallel(v_yo);
        }        
    }

    vector<Func> laplacianPyramid, combinedPyramid;
    
    //
    // Create laplacian pyramid
    //
    
    for(int level = 0; level < tonemap_levels; level++) {
        Func up("laplacianUpLvl" + std::to_string(level));
        Func upIntermediate("laplacianUpIntermediateLvl" + std::to_string(level));
        Func laplacian("laplacianLvl" + std::to_string(level));

        pyramidUp(up, upIntermediate, tonemapPyramid[level + 1].second);
        
        laplacian(v_x, v_y, v_c) = cast<int32_t>(tonemapPyramid[level].second(v_x, v_y, v_c)) - up(v_x, v_y, v_c);

        // Skip first level
        if(level > 0) {
            if(get_target().has_gpu_feature()) {
                up
                    .reorder(v_c, v_x, v_y)
                    .unroll(v_c)
                    .compute_at(laplacian, tile_idx)
                    .store_at(laplacian, tile_idx)
                    .gpu_threads(v_x, v_y);

                upIntermediate
                    .reorder(v_c, v_x, v_y)
                    .unroll(v_c)
                    .compute_at(laplacian, tile_idx)
                    .store_at(laplacian, tile_idx)
                    .gpu_threads(v_x, v_y);

                laplacian
                    .compute_root()
                    .reorder(v_c, v_x, v_y)
                    .tile(v_x, v_y, v_xo, v_yo, v_xi, v_yi, 16, 16)
                    .fuse(v_xo, v_yo, tile_idx)
                    .tile(v_xi, v_yi, v_xio, v_yio, v_xii, v_yii, 4, 4)
                    .fuse(v_xio, v_yio, subtile_idx)
                    .unroll(v_c)
                    .gpu_blocks(tile_idx)
                    .gpu_threads(subtile_idx);
            }
            else {
                up
                    .compute_at(laplacian, v_yi)
                    .store_at(laplacian, v_yo)
                    .unroll(v_c)
                    .vectorize(v_x, 12);

                upIntermediate
                    .compute_at(laplacian, v_yi)
                    .store_at(laplacian, v_yo)
                    .unroll(v_c)
                    .vectorize(v_x, 12);

                laplacian
                    .compute_root()
                    .reorder(v_c, v_x, v_y)
                    .split(v_y, v_yo, v_yi, 32)
                    .vectorize(v_x, 12)
                    .unroll(v_c)
                    .parallel(v_yo);
            }
        }
        
        laplacianPyramid.push_back(laplacian);
    }

    laplacianPyramid.push_back(tonemapPyramid[tonemap_levels].second);

    //
    // Combine pyramids
    //
    
    for(int level = 0; level <= tonemap_levels; level++) {
        Func result("resultLvl" + std::to_string(level));

        result(v_x, v_y, v_c) = cast<int32_t>(
            (laplacianPyramid[level](v_x, v_y, 0) * weightsPyramid[level].second(v_x, v_y, 0)) +
            (laplacianPyramid[level](v_x, v_y, 1) * weightsPyramid[level].second(v_x, v_y, 1)) + 0.5f
        );
        
        combinedPyramid.push_back(result);
    }

    //
    // Create output pyramid
    //
    
    vector<Func> outputPyramid;

    for(int level = tonemap_levels; level > 0; level--) {
        Func up("outputUpLvl" + std::to_string(level));
        Func upIntermediate("outputUpIntermediateLvl" + std::to_string(level));
        Func outputLvl("outputLvl" + std::to_string(level));

        if(level == tonemap_levels) {
            pyramidUp(up, upIntermediate, combinedPyramid[level]);
        }
        else {
            pyramidUp(up, upIntermediate, outputPyramid[outputPyramid.size() - 1]);
            
        }

        outputLvl(v_x, v_y, v_c) = combinedPyramid[level - 1](v_x, v_y, v_c) + up(v_x, v_y, v_c);

        // Skip last level
        if(get_target().has_gpu_feature()) {
            upIntermediate
                .reorder(v_c, v_x, v_y)
                .compute_at(outputLvl, v_x)
                .unroll(v_c)
                .gpu_threads(v_x, v_y);

            up
                .reorder(v_c, v_x, v_y)
                .compute_at(outputLvl, v_x)
                .unroll(v_c)
                .gpu_threads(v_x, v_y);

            outputLvl
                .compute_root()
                .reorder(v_c, v_x, v_y)
                .unroll(v_c)
                .gpu_tile(v_x, v_y, v_xi, v_yi, 16, 8);
        }
        else {
            upIntermediate
                .compute_at(outputLvl, subtile_idx)
                .store_at(outputLvl, tile_idx)
                .vectorize(v_x, 8);

            up
                .compute_at(outputLvl, subtile_idx)
                .store_at(outputLvl, tile_idx)
                .vectorize(v_x, 8);

            outputLvl
                .compute_root()
                .reorder(v_c, v_x, v_y)
                .unroll(v_c)
                .tile(v_x, v_y, v_xo, v_yo, v_xi, v_yi, 64, 64)
                .fuse(v_xo, v_yo, tile_idx)
                .tile(v_xi, v_yi, v_xio, v_yio, v_xii, v_yii, 32, 32)
                .fuse(v_xio, v_yio, subtile_idx)
                .parallel(tile_idx)
                .vectorize(v_xii, 16);
        }
    
        outputPyramid.push_back(outputLvl);
    }

    // Inverse gamma correct tonemapped result
    Func tonemapped("tonemapped");

    tonemapped(v_x, v_y) = inverseGammaLut(cast(output_type, clamp(outputPyramid[tonemap_levels - 1](v_x, v_y, 0), 0, type_max)));
    
    // Create output RGB image
    Expr uvScale = select(input(v_x, v_y, 0) == 0, 1.0f, cast<float>(tonemapped(v_x, v_y)) / input(v_x, v_y, 0));

    Expr U = uvScale * (input(v_x, v_y, 1) / cast<float>(type_max) - 0.5f) + 0.5f;
    Expr V = uvScale * (input(v_x, v_y, 2) / cast<float>(type_max) - 0.5f) + 0.5f;
    
    output(v_x, v_y, v_c) =
        select(v_c == 0, tonemapped(v_x, v_y),
               v_c == 1, saturating_cast(output_type, U * type_max),
                         saturating_cast(output_type, V * type_max));
}

void TonemapGenerator::schedule() { 
}

//

class PostProcessBase {
protected:
    void deinterleave(Func& result, Func in, int c, Expr stride, Expr rawFormat);
    void transform(Func& output, Func input, Func matrixSrgb);

    void rearrange(Func& output, Func input, Expr sensorArrangement);
    void rearrange(Func& output, Func in0, Func in1, Func in2, Func in3, Expr sensorArrangement);

    Func downsample(Func f, Func& temp);
    Func upsample(Func f, Func& temp);

    void weightedMedianFilter(Func& output, Func input);
    void medianFilter(Func& output, Func input);

    void rgb2yuv(Func& output, Func input);
    void yuv2rgb(Func& output, Func input);

    void cmpSwap(Expr& a, Expr& b);

    void rgbToHsv(Func& output, Func input);
    void hsvToBgr(Func& output, Func input);

    void shiftHues(
        Func& output, Func hsvInput, Expr blueSaturation, Expr greenSaturation, Expr overallSaturation);

    void linearScale(Func& result, Func image, Expr fromWidth, Expr fromHeight, Expr toWidth, Expr toHeight);
    
private:
    Func deinterleaveRaw16(Func in, int c, Expr stride);
    Func deinterleaveRaw10(Func in, int c, Expr stride);

protected:
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

void PostProcessBase::deinterleave(Func& result, Func in, int c, Expr stride, Expr rawFormat) {
    result(v_x, v_y) =
        select( rawFormat == static_cast<int>(RawFormat::RAW10), cast<uint16_t>(deinterleaveRaw10(in, c, stride)(v_x, v_y)),
                rawFormat == static_cast<int>(RawFormat::RAW16), cast<uint16_t>(deinterleaveRaw16(in, c, stride)(v_x, v_y)),
                0);
}

Func PostProcessBase::deinterleaveRaw16(Func in, int c, Expr stride) {
    Func result("deinterleaveRaw16Result");
    Func in32;

    in32(v_x) = cast<int32_t>(in(v_x));

    switch(c)
    {
        case 0:
            result(v_x, v_y) = in32(v_y*2 * stride + v_x*4 + 0) | (in32(v_y*2 * stride + v_x*4 + 1) << 8);
            break;

        case 1:
            result(v_x, v_y) = in32(v_y*2 * stride + v_x*4 + 2) | (in32(v_y*2 * stride + v_x*4 + 3) << 8);
            break;

        case 2:
            result(v_x, v_y) = in32((v_y*2 + 1) * stride + v_x*4 + 0) | (in32((v_y*2 + 1) * stride + v_x*4 + 1) << 8);
            break;

        case 3:
            result(v_x, v_y) = in32((v_y*2 + 1) * stride + v_x*4 + 2) | (in32((v_y*2 + 1) * stride + v_x*4 + 3) << 8);
        break;

        default:
            throw std::runtime_error("invalid channel");
    }

    return result;
}

Func PostProcessBase::deinterleaveRaw10(Func in, int c, Expr stride) {
    Func result("deinterleaveRaw10Result");

    Expr X = (v_y<<1) * stride + (v_x>>1) * 5;
    Expr Y = ((v_y<<1) + 1) * stride + (v_x>>1) * 5;

    switch(c)
    {
        case 0:
            result(v_x, v_y) =
                select((v_x & 1) == 0,
                    (cast<uint16_t>(in(X))     << 2) | (cast<uint16_t>(in(X + 4)) & 0x03),
                    (cast<uint16_t>(in(X + 2)) << 2) | (cast<uint16_t>(in(X + 4)) & 0x30) >> 4);
        break;

        case 1:
            result(v_x, v_y) =
                select((v_x & 1) == 0,
                    (cast<uint16_t>(in(X + 1)) << 2) | (cast<uint16_t>(in(X + 4)) & 0x0C) >> 2,
                    (cast<uint16_t>(in(X + 3)) << 2) | (cast<uint16_t>(in(X + 4)) & 0xC0) >> 6);
            break;

        case 2:
            result(v_x, v_y) =
                select((v_x & 1) == 0,
                    (cast<uint16_t>(in(Y))     << 2) | (cast<uint16_t>(in(Y + 4)) & 0x03),
                    (cast<uint16_t>(in(Y + 2)) << 2) | (cast<uint16_t>(in(Y + 4)) & 0x30) >> 4);        
        break;

        case 3:
        result(v_x, v_y) =
            select((v_x & 1) == 0,
                (cast<uint16_t>(in(Y + 1)) << 2) | (cast<uint16_t>(in(Y + 4)) & 0x0C) >> 2,
                (cast<uint16_t>(in(Y + 3)) << 2) | (cast<uint16_t>(in(Y + 4)) & 0xC0) >> 6);

        break;

        default:
            throw std::runtime_error("invalid channel");
    }

    return result;
}

Func PostProcessBase::downsample(Func f, Func& temp) {
    using Halide::_;
    Func in, downx, downy;
    
    in(v_x, v_y, _) = cast<int32_t>(f(v_x, v_y, _));

    temp(v_x, v_y, _) = (
        1 * in(v_x*2 - 1, v_y, _) +
        2 * in(v_x*2,     v_y, _) +
        1 * in(v_x*2 + 1, v_y, _) ) >> 2;

    downy(v_x, v_y, _) = cast<uint16_t> (
       (
        1 * temp(v_x, v_y*2 - 1, _) +
        2 * temp(v_x, v_y*2,     _) +
        1 * temp(v_x, v_y*2 + 1, _)
       ) >> 2
    );
    
    return downy;
}

Func PostProcessBase::upsample(Func f, Func& temp) {
    using Halide::_;
    Func in, upx, upy;
    
    in(v_x, v_y, _) = cast<int32_t>(f(v_x, v_y, _));

    temp(v_x, v_y, _) = (
        1 * in(v_x/2 - 1, v_y, _) +
        2 * in(v_x/2,     v_y, _) +
        1 * in(v_x/2 + 1, v_y, _)) >> 2;

    upy(v_x, v_y, _) = cast<uint16_t> (
       (1 * temp(v_x, v_y/2 - 1, _) +
        2 * temp(v_x, v_y/2,     _) +
        1 * temp(v_x, v_y/2 + 1, _)) >> 2
    );

    return upy;
}

void PostProcessBase::transform(Func& output, Func input, Func m) {
    Expr ir = input(v_x, v_y, 0);
    Expr ig = input(v_x, v_y, 1);
    Expr ib = input(v_x, v_y, 2);

    // Color correct
    Expr r = m(0, 0) * ir + m(1, 0) * ig + m(2, 0) * ib;
    Expr g = m(0, 1) * ir + m(1, 1) * ig + m(2, 1) * ib;
    Expr b = m(0, 2) * ir + m(1, 2) * ig + m(2, 2) * ib;
    
    output(v_x, v_y, v_c) = select(v_c == 0, clamp(r, 0.0f, 1.0f),
                                   v_c == 1, clamp(g, 0.0f, 1.0f),
                                             clamp(b, 0.0f, 1.0f));
}

void PostProcessBase::rearrange(Func& output, Func in0, Func in1, Func in2, Func in3, Expr sensorArrangement) {
    output(v_x, v_y, v_c) =
        select(sensorArrangement == static_cast<int>(SensorArrangement::RGGB),
                select( v_c == 0, in0(v_x, v_y),
                        v_c == 1, in1(v_x, v_y),
                        v_c == 2, in2(v_x, v_y),
                                  in3(v_x, v_y) ),

            sensorArrangement == static_cast<int>(SensorArrangement::GRBG),
                select( v_c == 0, in1(v_x, v_y),
                        v_c == 1, in0(v_x, v_y),
                        v_c == 2, in3(v_x, v_y),
                                  in2(v_x, v_y) ),

            sensorArrangement == static_cast<int>(SensorArrangement::GBRG),
                select( v_c == 0, in2(v_x, v_y),
                        v_c == 1, in0(v_x, v_y),
                        v_c == 2, in3(v_x, v_y),
                                  in1(v_x, v_y) ),

                select( v_c == 0, in3(v_x, v_y),
                        v_c == 1, in1(v_x, v_y),
                        v_c == 2, in2(v_x, v_y),
                                  in0(v_x, v_y) ) );

}

void PostProcessBase::rearrange(Func& output, Func input, Expr sensorArrangement) {
    output(v_x, v_y, v_c) =
        select(sensorArrangement == static_cast<int>(SensorArrangement::RGGB),
                select( v_c == 0, input(v_x, v_y, 0),
                        v_c == 1, input(v_x, v_y, 1),
                        v_c == 2, input(v_x, v_y, 2),
                                  input(v_x, v_y, 3) ),

            sensorArrangement == static_cast<int>(SensorArrangement::GRBG),
                select( v_c == 0, input(v_x, v_y, 1),
                        v_c == 1, input(v_x, v_y, 0),
                        v_c == 2, input(v_x, v_y, 3),
                                  input(v_x, v_y, 2) ),

            sensorArrangement == static_cast<int>(SensorArrangement::GBRG),
                select( v_c == 0, input(v_x, v_y, 2),
                        v_c == 1, input(v_x, v_y, 0),
                        v_c == 2, input(v_x, v_y, 3),
                                  input(v_x, v_y, 1) ),

                select( v_c == 0, input(v_x, v_y, 3),
                        v_c == 1, input(v_x, v_y, 1),
                        v_c == 2, input(v_x, v_y, 2),
                                  input(v_x, v_y, 0) ) );

}

void PostProcessBase::medianFilter(Func& output, Func input) {
    Expr p[25];

    // 3x3 median filter
    for(int y = -2; y <= 2; y++) {
        for(int x = -2; x <= 2; x++) {
            p[5*(y+2)+(x+2)] = input(v_x + y, v_y + y);
        }
    }

    cmpSwap(p[1], p[2]);
    cmpSwap(p[0], p[2]);
    cmpSwap(p[0], p[1]);
    cmpSwap(p[4], p[5]);
    cmpSwap(p[3], p[5]);
    cmpSwap(p[3], p[4]);
    cmpSwap(p[0], p[3]);
    cmpSwap(p[1], p[4]);
    cmpSwap(p[2], p[5]);
    cmpSwap(p[2], p[4]);
    cmpSwap(p[1], p[3]);
    cmpSwap(p[2], p[3]);
    cmpSwap(p[7], p[8]);
    cmpSwap(p[6], p[8]);
    cmpSwap(p[6], p[7]);
    cmpSwap(p[10], p[11]);
    cmpSwap(p[9], p[11]);
    cmpSwap(p[9], p[10]);
    cmpSwap(p[6], p[9]);
    cmpSwap(p[7], p[10]);
    cmpSwap(p[8], p[11]);
    cmpSwap(p[8], p[10]);
    cmpSwap(p[7], p[9]);
    cmpSwap(p[8], p[9]);
    cmpSwap(p[0], p[6]);
    cmpSwap(p[1], p[7]);
    cmpSwap(p[2], p[8]);
    cmpSwap(p[2], p[7]);
    cmpSwap(p[1], p[6]);
    cmpSwap(p[2], p[6]);
    cmpSwap(p[3], p[9]);
    cmpSwap(p[4], p[10]);
    cmpSwap(p[5], p[11]);
    cmpSwap(p[5], p[10]);
    cmpSwap(p[4], p[9]);
    cmpSwap(p[5], p[9]);
    cmpSwap(p[3], p[6]);
    cmpSwap(p[4], p[7]);
    cmpSwap(p[5], p[8]);
    cmpSwap(p[5], p[7]);
    cmpSwap(p[4], p[6]);
    cmpSwap(p[5], p[6]);
    cmpSwap(p[13], p[14]);
    cmpSwap(p[12], p[14]);
    cmpSwap(p[12], p[13]);
    cmpSwap(p[16], p[17]);
    cmpSwap(p[15], p[17]);
    cmpSwap(p[15], p[16]);
    cmpSwap(p[12], p[15]);
    cmpSwap(p[13], p[16]);
    cmpSwap(p[14], p[17]);
    cmpSwap(p[14], p[16]);
    cmpSwap(p[13], p[15]);
    cmpSwap(p[14], p[15]);
    cmpSwap(p[19], p[20]);
    cmpSwap(p[18], p[20]);
    cmpSwap(p[18], p[19]);
    cmpSwap(p[21], p[22]);
    cmpSwap(p[23], p[24]);
    cmpSwap(p[21], p[23]);
    cmpSwap(p[22], p[24]);
    cmpSwap(p[22], p[23]);
    cmpSwap(p[18], p[22]);
    cmpSwap(p[18], p[21]);
    cmpSwap(p[19], p[23]);
    cmpSwap(p[20], p[24]);
    cmpSwap(p[20], p[23]);
    cmpSwap(p[19], p[21]);
    cmpSwap(p[20], p[22]);
    cmpSwap(p[20], p[21]);
    cmpSwap(p[12], p[19]);
    cmpSwap(p[12], p[18]);
    cmpSwap(p[13], p[20]);
    cmpSwap(p[14], p[21]);
    cmpSwap(p[14], p[20]);
    cmpSwap(p[13], p[18]);
    cmpSwap(p[14], p[19]);
    cmpSwap(p[14], p[18]);
    cmpSwap(p[15], p[22]);
    cmpSwap(p[16], p[23]);
    cmpSwap(p[17], p[24]);
    cmpSwap(p[17], p[23]);
    cmpSwap(p[16], p[22]);
    cmpSwap(p[17], p[22]);
    cmpSwap(p[15], p[19]);
    cmpSwap(p[15], p[18]);
    cmpSwap(p[16], p[20]);
    cmpSwap(p[17], p[21]);
    cmpSwap(p[17], p[20]);
    cmpSwap(p[16], p[18]);
    cmpSwap(p[17], p[19]);
    cmpSwap(p[17], p[18]);
    cmpSwap(p[0], p[13]);
    cmpSwap(p[0], p[12]);
    cmpSwap(p[1], p[14]);
    cmpSwap(p[2], p[15]);
    cmpSwap(p[2], p[14]);
    cmpSwap(p[1], p[12]);
    cmpSwap(p[2], p[13]);
    cmpSwap(p[2], p[12]);
    cmpSwap(p[3], p[16]);
    cmpSwap(p[4], p[17]);
    cmpSwap(p[5], p[18]);
    cmpSwap(p[5], p[17]);
    cmpSwap(p[4], p[16]);
    cmpSwap(p[5], p[16]);
    cmpSwap(p[3], p[13]);
    cmpSwap(p[3], p[12]);
    cmpSwap(p[4], p[14]);
    cmpSwap(p[5], p[15]);
    cmpSwap(p[5], p[14]);
    cmpSwap(p[4], p[12]);
    cmpSwap(p[5], p[13]);
    cmpSwap(p[5], p[12]);
    cmpSwap(p[6], p[19]);
    cmpSwap(p[7], p[20]);
    cmpSwap(p[8], p[21]);
    cmpSwap(p[8], p[20]);
    cmpSwap(p[7], p[19]);
    cmpSwap(p[8], p[19]);
    cmpSwap(p[9], p[22]);
    cmpSwap(p[10], p[23]);
    cmpSwap(p[11], p[24]);
    cmpSwap(p[11], p[23]);
    cmpSwap(p[10], p[22]);
    cmpSwap(p[11], p[22]);
    cmpSwap(p[9], p[19]);
    cmpSwap(p[10], p[20]);
    cmpSwap(p[11], p[21]);
    cmpSwap(p[11], p[20]);
    cmpSwap(p[10], p[19]);
    cmpSwap(p[11], p[19]);
    cmpSwap(p[6], p[13]);
    cmpSwap(p[6], p[12]);
    cmpSwap(p[7], p[14]);
    cmpSwap(p[8], p[15]);
    cmpSwap(p[8], p[14]);
    cmpSwap(p[7], p[12]);
    cmpSwap(p[8], p[13]);
    cmpSwap(p[8], p[12]);
    cmpSwap(p[9], p[16]);
    cmpSwap(p[10], p[17]);
    cmpSwap(p[11], p[18]);
    cmpSwap(p[11], p[17]);
    cmpSwap(p[10], p[16]);
    cmpSwap(p[11], p[16]);
    cmpSwap(p[9], p[13]);
    cmpSwap(p[9], p[12]);
    cmpSwap(p[10], p[14]);
    cmpSwap(p[11], p[15]);
    cmpSwap(p[11], p[14]);
    cmpSwap(p[10], p[12]);
    cmpSwap(p[11], p[13]);
    cmpSwap(p[11], p[12]);

    output(v_x, v_y) = p[11];
}

void PostProcessBase::weightedMedianFilter(Func& output, Func input) {

    Expr p0 = input(v_x,   v_y);
    Expr p1 = input(v_x,   v_y);
    Expr p2 = input(v_x,   v_y);
    Expr p3 = input(v_x,   v_y);

    Expr p4 = input(v_x-1, v_y);
    Expr p5 = input(v_x-1, v_y);

    Expr p6 = input(v_x+1, v_y);
    Expr p7 = input(v_x+1, v_y);

    Expr p8 = input(v_x,   v_y-1);
    Expr p9 = input(v_x,   v_y-1);

    Expr p10 = input(v_x,  v_y+1);
    Expr p11 = input(v_x,  v_y+1);

    Expr p12 = input(v_x-2, v_y);
    Expr p13 = input(v_x+2, v_y);

    Expr p14 = input(v_x,   v_y-2);
    Expr p15 = input(v_x,   v_y+2);

    Expr p16 = input(v_x-1, v_y-1);
    Expr p17 = input(v_x-1, v_y+1);

    Expr p18 = input(v_x+1, v_y-1);
    Expr p19 = input(v_x+1, v_y+1);

    cmpSwap(p0, p1);
    cmpSwap(p3, p4);
    cmpSwap(p2, p4);
    cmpSwap(p2, p3);
    cmpSwap(p0, p3);
    cmpSwap(p0, p2);
    cmpSwap(p1, p4);
    cmpSwap(p1, p3);
    cmpSwap(p1, p2);
    cmpSwap(p5, p6);
    cmpSwap(p8, p9);
    cmpSwap(p7, p9);
    cmpSwap(p7, p8);
    cmpSwap(p5, p8);
    cmpSwap(p5, p7);
    cmpSwap(p6, p9);
    cmpSwap(p6, p8);
    cmpSwap(p6, p7);
    cmpSwap(p0, p5);
    cmpSwap(p1, p6);
    cmpSwap(p1, p5);
    cmpSwap(p2, p7);
    cmpSwap(p3, p8);
    cmpSwap(p4, p9);
    cmpSwap(p4, p8);
    cmpSwap(p3, p7);
    cmpSwap(p4, p7);
    cmpSwap(p2, p5);
    cmpSwap(p3, p6);
    cmpSwap(p4, p6);
    cmpSwap(p3, p5);
    cmpSwap(p4, p5);
    cmpSwap(p10, p11);
    cmpSwap(p13, p14);
    cmpSwap(p12, p14);
    cmpSwap(p12, p13);
    cmpSwap(p10, p13);
    cmpSwap(p10, p12);
    cmpSwap(p11, p14);
    cmpSwap(p11, p13);
    cmpSwap(p11, p12);
    cmpSwap(p15, p16);
    cmpSwap(p18, p19);
    cmpSwap(p17, p19);
    cmpSwap(p17, p18);
    cmpSwap(p15, p18);
    cmpSwap(p15, p17);
    cmpSwap(p16, p19);
    cmpSwap(p16, p18);
    cmpSwap(p16, p17);
    cmpSwap(p10, p15);
    cmpSwap(p11, p16);
    cmpSwap(p11, p15);
    cmpSwap(p12, p17);
    cmpSwap(p13, p18);
    cmpSwap(p14, p19);
    cmpSwap(p14, p18);
    cmpSwap(p13, p17);
    cmpSwap(p14, p17);
    cmpSwap(p12, p15);
    cmpSwap(p13, p16);
    cmpSwap(p14, p16);
    cmpSwap(p13, p15);
    cmpSwap(p14, p15);
    cmpSwap(p0, p10);
    cmpSwap(p1, p11);
    cmpSwap(p1, p10);
    cmpSwap(p2, p12);
    cmpSwap(p3, p13);
    cmpSwap(p4, p14);
    cmpSwap(p4, p13);
    cmpSwap(p3, p12);
    cmpSwap(p4, p12);
    cmpSwap(p2, p10);
    cmpSwap(p3, p11);
    cmpSwap(p4, p11);
    cmpSwap(p3, p10);
    cmpSwap(p4, p10);
    cmpSwap(p5, p15);
    cmpSwap(p6, p16);
    cmpSwap(p6, p15);
    cmpSwap(p7, p17);
    cmpSwap(p8, p18);
    cmpSwap(p9, p19);
    cmpSwap(p9, p18);
    cmpSwap(p8, p17);
    cmpSwap(p9, p17);
    cmpSwap(p7, p15);
    cmpSwap(p8, p16);
    cmpSwap(p9, p16);
    cmpSwap(p8, p15);
    cmpSwap(p9, p15);
    cmpSwap(p5, p10);
    cmpSwap(p6, p11);
    cmpSwap(p6, p10);
    cmpSwap(p7, p12);
    cmpSwap(p8, p13);
    cmpSwap(p9, p14);
    cmpSwap(p9, p13);
    cmpSwap(p8, p12);
    cmpSwap(p9, p12);
    cmpSwap(p7, p10);
    cmpSwap(p8, p11);
    cmpSwap(p9, p11);
    cmpSwap(p8, p10);
    cmpSwap(p9, p10);

    output(v_x, v_y) = cast<uint16_t>((cast<int32_t>(p9) + cast<int32_t>(p10)) / 2);
}

void PostProcessBase::rgb2yuv(Func& output, Func input) {
    Expr R = input(v_x, v_y, 0);
    Expr G = input(v_x, v_y, 1);
    Expr B = input(v_x, v_y, 2);

    Expr Y = YUV_R*R + YUV_G*G + YUV_B*B;
    Expr U = 0.5f * (B - Y) / (1 - YUV_B) + 0.5f;
    Expr V = 0.5f * (R - Y) / (1 - YUV_R) + 0.5f;

    output(v_x, v_y, v_c) =
        select(v_c == 0, Y,
               v_c == 1, U,
                         V);
}

void PostProcessBase::yuv2rgb(Func& output, Func input) {
    Expr Y = input(v_x, v_y, 0);
    Expr U = input(v_x, v_y, 1);
    Expr V = input(v_x, v_y, 2);

    Expr R = Y + 2*(V - 0.5f) * (1 - YUV_R);
    Expr G = Y - 2*(U - 0.5f) * (1 - YUV_B) * YUV_B / YUV_G - 2*(V - 0.5f) * (1 - YUV_R) * YUV_R / YUV_G;
    Expr B = Y + 2*(U - 0.5f) * (1 - YUV_B);

    output(v_x, v_y, v_c) =
        select(v_c == 0, R,
               v_c == 1, G,
                         B);
}

void PostProcessBase::rgbToHsv(Func& output, Func input) {
    const float eps = std::numeric_limits<float>::epsilon();

    Expr r = cast<float>(input(v_x, v_y, 0));
    Expr g = cast<float>(input(v_x, v_y, 1));
    Expr b = cast<float>(input(v_x, v_y, 2));

    Expr maxRgb = max(r, g, b);
    Expr min0gb = min(r, g, b);

    Expr delta = maxRgb - min0gb;
    
    Expr h = select(abs(delta) < eps, 0.0f,
                    maxRgb == r, ((g - b) / delta) % 6,
                    maxRgb == g, 2.0f + (b - r) / delta,
                                 4.0f + (r - g) / delta);

    Expr s = select(abs(maxRgb) < eps, 0.0f, delta / maxRgb);
    Expr v = maxRgb;

    output(v_x, v_y, v_c) = select(v_c == 0, 60.0f * h,
                                   v_c == 1, s,
                                             v);
}

void PostProcessBase::hsvToBgr(Func& output, Func input) {
    Expr H = cast<float>(input(v_x, v_y, 0));
    Expr S = cast<float>(input(v_x, v_y, 1));
    Expr V = cast<float>(input(v_x, v_y, 2));

    Expr h = H / 60.0f;
    Expr i = cast<int>(h);
    
    Expr f = h - i;
    Expr p = V * (1.0f - S);
    Expr q = V * (1.0f - S * f);
    Expr t = V * (1.0f - S * (1.0f - f));
    
    Expr r = select(i == 0, V,
                    i == 1, q,
                    i == 2, p,
                    i == 3, p,
                    i == 4, t,
                            V);
    
    Expr g = select(i == 0, t,
                    i == 1, V,
                    i == 2, V,
                    i == 3, q,
                    i == 4, p,
                            p);

    Expr b = select(i == 0, p,
                    i == 1, p,
                    i == 2, t,
                    i == 3, V,
                    i == 4, V,
                            q);

    output(v_x, v_y, v_c) = select(v_c == 0, clamp(b, 0.0f, 1.0f),
                                   v_c == 1, clamp(g, 0.0f, 1.0f),
                                             clamp(r, 0.0f, 1.0f));
}

void PostProcessBase::cmpSwap(Expr& a, Expr& b) {
    Expr tmp = min(a, b);
    b = max(a, b);
    a = tmp;
}

void PostProcessBase::linearScale(Func& result, Func image, Expr fromWidth, Expr fromHeight, Expr toWidth, Expr toHeight) {
    Expr scaleX = toWidth * fast_inverse(cast<float>(fromWidth));
    Expr scaleY = toHeight * fast_inverse(cast<float>(fromHeight));
    
    Expr fx = max(0, (v_x + 0.5f) * fast_inverse(scaleX) - 0.5f);
    Expr fy = max(0, (v_y + 0.5f) * fast_inverse(scaleY) - 0.5f);
    
    Expr x = cast<int>(fx);
    Expr y = cast<int>(fy);
    
    Expr a = fx - x;
    Expr b = fy - y;

    Expr x0 = clamp(x, 0, fromWidth - 1);
    Expr y0 = clamp(y, 0, fromHeight - 1);

    Expr x1 = clamp(x + 1, 0, fromWidth - 1);
    Expr y1 = clamp(y + 1, 0, fromHeight - 1);
    
    Expr p0 = lerp(cast<float>(image(x0, y0)), cast<float>(image(x1, y0)), a);
    Expr p1 = lerp(cast<float>(image(x0, y1)), cast<float>(image(x1, y1)), a);

    result(v_x, v_y) = lerp(p0, p1, b);
}

void PostProcessBase::shiftHues(
    Func& output, Func hsvInput, Expr blueSaturation, Expr greenSaturation, Expr overallSaturation)
{
    Expr H = hsvInput(v_x, v_y, 0);
    Expr S = hsvInput(v_x, v_y, 1);

    Expr sat = select(
                        // Blues
                        H >= 145 && H < 165,    S * (1.0f/20.0f * ( (blueSaturation - 1)*H - 145 * (blueSaturation - 1) ) + 1),
                        H >= 165 && H <= 195,   S * blueSaturation,
                        H  > 195 && H <= 215,   S * (-1.0f/20.0f * ( (blueSaturation - 1)*H - 215 * (blueSaturation - 1) ) + 1),

                        // Greens
                        H >= 70 && H < 90,      S * (1.0f/20.0f * ( (greenSaturation - 1)*H - 70 * (greenSaturation - 1) ) + 1),
                        H >= 90 && H <= 135,    S * greenSaturation,
                        H  > 135 && H <= 155,   S * (-1.0f/20.0f * ( (greenSaturation - 1)*H - 155 * (greenSaturation - 1) ) + 1),

                        S
                    );

    output(v_x, v_y, v_c) =
        select(v_c == 0, hsvInput(v_x, v_y, 0),
               v_c == 1, clamp(sat * overallSaturation, 0.0f, 1.0f),
                         hsvInput(v_x, v_y, v_c));    
}

//

class PostProcessGenerator : public Halide::Generator<PostProcessGenerator>, public PostProcessBase {
public:
    Input<Buffer<uint16_t>> in0{"in0", 2 };
    Input<Buffer<uint16_t>> in1{"in1", 2 };
    Input<Buffer<uint16_t>> in2{"in2", 2 };
    Input<Buffer<uint16_t>> in3{"in3", 2 };

    Input<Buffer<float>> inshadingMap0{"inshadingMap0", 2 };
    Input<Buffer<float>> inshadingMap1{"inshadingMap1", 2 };
    Input<Buffer<float>> inshadingMap2{"inshadingMap2", 2 };
    Input<Buffer<float>> inshadingMap3{"inshadingMap3", 2 };
    
    Input<float> asShotVector0{"asShotVector0"};
    Input<float> asShotVector1{"asShotVector1"};
    Input<float> asShotVector2{"asShotVector2"};

    Input<Buffer<float>> cameraToSrgb{"cameraToSrgb", 2};

    Input<int16_t> range{"range"};
    Input<int> sensorArrangement{"sensorArrangement"};
    
    Input<float> gamma{"gamma"};
    Input<float> shadows{"shadows"};
    Input<float> tonemapVariance{"tonemapVariance"};
    Input<float> blacks{"blacks"};
    Input<float> exposure{"exposure"};
    Input<float> whitePoint{"whitePoint"};
    Input<float> contrast{"contrast"};
    Input<float> blueSaturation{"blueSaturation"};
    Input<float> saturation{"saturation"};
    Input<float> greenSaturation{"greenSaturation"};
    Input<float> sharpen0{"sharpen0"};
    Input<float> sharpen1{"sharpen1"};
    Input<float> chromaFilterWeight{"chromaFilterWeight"};
        
    Output<Buffer<uint8_t>> output{"output", 3};
    
    Func clamped0{"clamped0"};
    Func clamped1{"clamped1"};
    Func clamped2{"clamped2"};
    Func clamped3{"clamped3"};

    Func combinedInput{"combinedInput"};
    Func mirroredInput{"mirroredInput"};
    Func bayerInput{"bayerInput"};
    Func adjustExposure{"adjustExposure"};
    Func colorCorrected{"colorCorrected"};
    Func colorCorrectedYuv{"colorCorrectedYuv"};
    Func Yfiltered{"Yfiltered"}, Udownsampled{"Udownsampled"}, Vdownsampled{"Vdownsampled"};
    Func uvDownsampled{"uvDownsampled"};
    Func tonemapIn{"tonemapIn"};
    Func tonemapOutputRgb{"tonemapOutputRgb"};
    Func gammaCorrected{"gammaCorrected"};
    Func sharpened{"sharpened"};
    Func sharpenInputY{"sharpenInputY"};
    Func chromaDenoiseInputU{"chromaDenoiseInputU"}, chromaDenoiseInputV{"chromaDenoiseInputV"};
    Func finalTonemap{"finalTonemap"};
    Func hsvInput{"hsvInput"};
    Func saturationValue{"saturationValue"}, saturationFiltered{"saturationFiltered"};
    Func saturationApplied{"saturationApplied"};
    Func finalRgb{"finalRgb"};
    Func gammaContrastLut{"gammaContrastLut"};

    Func shaded{"shaded"};
    Func shadingMap0{"shadingMap0"}, shadingMap1{"shadingMap1"}, shadingMap2{"shadingMap2"}, shadingMap3{"shadingMap3"};
    Func shadingMapArranged{"shadingMapArranged"};

    std::unique_ptr<Demosaic> demosaic;
    std::unique_ptr<TonemapGenerator> tonemap;
    std::unique_ptr<GuidedFilter> guidedFilter0;
    std::unique_ptr<GuidedFilter> guidedFilter1;

    std::unique_ptr<GuidedFilter> sharpenGf0;
    std::unique_ptr<GuidedFilter> sharpenGf1;

    Func gaussianOne, gaussianTwo, gaussianThree;
    
    void generate();
    void schedule_for_gpu();
    void schedule_for_cpu();
    
private:
    void blur(Func& output, Func input);
    void blur2(Func& output, Func input);
    void blur3(Func& output, Func input);
};

void PostProcessGenerator::blur(Func& output, Func input) {
    Func in32;
    Func blurX;

    in32(v_x, v_y) = cast<int32_t>(input(v_x, v_y));
    
    blurX(v_x, v_y) = (
        1 * in32(v_x - 1, v_y) +
        2 * in32(v_x    , v_y) +
        1 * in32(v_x + 1, v_y)
    ) / 4;

    output(v_x, v_y) =
        cast<uint16_t> (
            (
              1 * blurX(v_x, v_y - 1) +
              2 * blurX(v_x, v_y)     +
              1 * blurX(v_x, v_y + 1)             
            ) / 4
        );
}

void PostProcessGenerator::blur2(Func& output, Func input) {
    Func in32;
    Func blurX;

    in32(v_x, v_y) = cast<int32_t>(input(v_x, v_y));
    
    blurX(v_x, v_y) = (
        1 * in32(v_x - 2, v_y) +
        4 * in32(v_x - 1, v_y) +
        6 * in32(v_x,     v_y) +
        4 * in32(v_x + 1, v_y) +
        1 * in32(v_x + 2, v_y)
    ) / 16;

    output(v_x, v_y) =
        cast<uint16_t> (
            (
              1 * blurX(v_x, v_y - 2) +
              4 * blurX(v_x, v_y - 1) +
              6 * blurX(v_x, v_y)     +
              4 * blurX(v_x, v_y + 1) +
              1 * blurX(v_x, v_y + 2)             
            ) / 16
        );
}

void PostProcessGenerator::blur3(Func& output, Func input) {
    Func in32;
    Func blurX;

    in32(v_x, v_y) = cast<int32_t>(input(v_x, v_y));
    
    // 1+8+28+56+70+56+28+8+1 = 256

    blurX(v_x, v_y) = (
        1  * in32(v_x - 4, v_y) +
        8  * in32(v_x - 3, v_y) +
        28 * in32(v_x - 2, v_y) +
        56 * in32(v_x - 1, v_y) +
        70 * in32(v_x,     v_y) +
        56 * in32(v_x + 1, v_y) +
        28 * in32(v_x + 2, v_y) +
        8  * in32(v_x + 3, v_y) +
        1  * in32(v_x + 4, v_y)
    ) / 256;

    output(v_x, v_y) =
        cast<uint16_t> ((
            1  * in32(v_x, v_y - 4) +
            8  * in32(v_x, v_y - 3) +
            28 * in32(v_x, v_y - 2) +
            56 * in32(v_x, v_y - 1) +
            70 * in32(v_x, v_y)     +
            56 * in32(v_x, v_y + 1) +
            28 * in32(v_x, v_y + 2) +
            8  * in32(v_x, v_y + 3) +
            1  * in32(v_x, v_y + 4)
            ) / 256
        );
}

void PostProcessGenerator::generate()
{
    Expr sharpen0Param = sharpen0;
    Expr sharpen1Param = sharpen1;
    Expr shadowsParam  = shadows;
    Expr blacksParam   = blacks;
    Expr exposureParam = pow(2.0f, exposure);
    Expr satParam      = saturation;

    clamped0 = BoundaryConditions::repeat_edge(in0);
    clamped1 = BoundaryConditions::repeat_edge(in1);
    clamped2 = BoundaryConditions::repeat_edge(in2);
    clamped3 = BoundaryConditions::repeat_edge(in3);
        
    linearScale(shadingMap0, inshadingMap0, inshadingMap0.width(), inshadingMap0.height(), in0.width(), in0.height());
    linearScale(shadingMap1, inshadingMap1, inshadingMap1.width(), inshadingMap1.height(), in1.width(), in1.height());
    linearScale(shadingMap2, inshadingMap2, inshadingMap2.width(), inshadingMap2.height(), in2.width(), in2.height());
    linearScale(shadingMap3, inshadingMap3, inshadingMap3.width(), inshadingMap3.height(), in3.width(), in3.height());

    rearrange(shadingMapArranged, shadingMap0, shadingMap1, shadingMap2, shadingMap3, sensorArrangement);

    shaded(v_x, v_y, v_c) = 
        select( v_c == 0, cast<int16_t>( clamp( clamped0(v_x, v_y) * shadingMapArranged(v_x, v_y, 0), 0, range) ),
                v_c == 1, cast<int16_t>( clamp( clamped1(v_x, v_y) * shadingMapArranged(v_x, v_y, 1), 0, range) ),
                v_c == 2, cast<int16_t>( clamp( clamped2(v_x, v_y) * shadingMapArranged(v_x, v_y, 2), 0, range) ),
                          cast<int16_t>( clamp( clamped3(v_x, v_y) * shadingMapArranged(v_x, v_y, 3), 0, range) ) );

    // Combined image
    combinedInput(v_x, v_y) =
        select(v_y % 2 == 0,
               select(v_x % 2 == 0, shaded(v_x/2, v_y/2, 0), shaded(v_x/2, v_y/2, 1)),
               select(v_x % 2 == 0, shaded(v_x/2, v_y/2, 2), shaded(v_x/2, v_y/2, 3)));

    mirroredInput = BoundaryConditions::mirror_image(combinedInput, { {0, in0.width()*2}, {0, in0.height()*2} } );

    bayerInput(v_x, v_y) =
        select(sensorArrangement == static_cast<int>(SensorArrangement::RGGB),
                mirroredInput(v_x, v_y),

            sensorArrangement == static_cast<int>(SensorArrangement::GRBG),
                mirroredInput(v_x - 1, v_y),

            sensorArrangement == static_cast<int>(SensorArrangement::GBRG),
                mirroredInput(v_x, v_y - 1),

                // BGGR
                mirroredInput(v_x - 1, v_y - 1));

    // Demosaic image
    demosaic = create<Demosaic>();
    demosaic->apply(bayerInput, in0.width(), in0.height(), sensorArrangement);
    
    // Transform to sRGB space
    Func linear("linear"), colorCorrectInput("colorCorrectInput");

    linear(v_x, v_y, v_c) = (demosaic->output(v_x, v_y, v_c) / cast<float>(range));

    colorCorrectInput(v_x, v_y, v_c) =
        select( v_c == 0, clamp( linear(v_x, v_y, 0), 0.0f, asShotVector0 ),
                v_c == 1, clamp( linear(v_x, v_y, 1), 0.0f, asShotVector1 ),
                          clamp( linear(v_x, v_y, 2), 0.0f, asShotVector2 ));

    transform(colorCorrected, colorCorrectInput, cameraToSrgb);

    // Adjust exposure
    adjustExposure(v_x, v_y, v_c) = clamp(exposureParam * colorCorrected(v_x, v_y, v_c), 0.0f, 1.0f);

    // Move to YUV space
    Func yuvResult("yuvResult");

    rgb2yuv(yuvResult, adjustExposure);

    colorCorrectedYuv(v_x, v_y, v_c) = cast<uint16_t>(clamp(yuvResult(v_x, v_y, v_c) * 65535, 0, 65535));

    Func Y("Y"), U("U"), V("V");

    Y(v_x, v_y) = colorCorrectedYuv(v_x, v_y, 0);
    U(v_x, v_y) = colorCorrectedYuv(v_x, v_y, 1);
    V(v_x, v_y) = colorCorrectedYuv(v_x, v_y, 2);

    // Fix any small artifacts by median filtering
    weightedMedianFilter(Yfiltered, Y);

    Func Utemp, Vtemp;

    Udownsampled = downsample(U, Utemp);
    Vdownsampled = downsample(V, Vtemp);

    uvDownsampled(v_x, v_y, v_c) = select(v_c == 0, Udownsampled(v_x, v_y), Vdownsampled(v_x, v_y));

    Func Udenoise, Vdenoise;
    Func UdenoiseTemp, VdenoiseTemp;
    
    Udenoise.define_extern("extern_denoise", { uvDownsampled, in0.width(), in0.height(), 0, chromaFilterWeight}, UInt(16), 2);
    Udenoise.compute_root();

    Vdenoise.define_extern("extern_denoise", { uvDownsampled, in0.width(), in0.height(), 1, chromaFilterWeight}, UInt(16), 2);
    Vdenoise.compute_root();

    tonemapIn(v_x, v_y, v_c) = select(v_c == 0, cast<uint16_t>(Yfiltered(v_x, v_y)),
                                      v_c == 1, cast<uint16_t>(clamp(upsample(Udenoise, UdenoiseTemp)(v_x, v_y), 0, 65535)),
                                                cast<uint16_t>(clamp(upsample(Vdenoise, VdenoiseTemp)(v_x, v_y), 0, 65535)));

    tonemap = create<TonemapGenerator>();

    tonemap->output_type.set(UInt(16));
    tonemap->tonemap_levels.set(TONEMAP_LEVELS);
    tonemap->apply(tonemapIn, in0.width() * 2, in0.height() * 2, tonemapVariance, gamma, shadowsParam);

    //
    // Sharpen
    //

    sharpenInputY(v_x, v_y) = tonemap->output(v_x, v_y, 0);

    sharpenGf0 = create<GuidedFilter>();
    sharpenGf0->radius.set(SHARPEN_FILTER_RADIUS);
    sharpenGf0->apply(sharpenInputY, 0.1f*0.1f * 65535*65535);

    sharpenGf1 = create<GuidedFilter>();
    sharpenGf1->radius.set(DETAIL_FILTER_RADIUS);
    sharpenGf1->apply(sharpenGf0->output, 0.1f*0.1f * 65535*65535);
    
    Func gaussianDiff0, gaussianDiff1;
    
    gaussianDiff0(v_x, v_y) = cast<int32_t>(sharpenInputY(v_x, v_y)) - sharpenGf0->output(v_x, v_y);
    gaussianDiff1(v_x, v_y) = cast<int32_t>(sharpenGf0->output(v_x, v_y))  - sharpenGf1->output(v_x, v_y);
    
    sharpened(v_x, v_y) =
        saturating_cast<int32_t>(
            sharpenGf1->output(v_x, v_y) +
            sharpen0Param*gaussianDiff0(v_x, v_y) +
            sharpen1Param*gaussianDiff1(v_x, v_y)
        );

    // Back to RGB
    finalTonemap(v_x, v_y, v_c) = select(v_c == 0, sharpened(v_x, v_y) / 65535.0f,
                                         v_c == 1, tonemap->output(v_x, v_y, 1) / 65535.0f,
                                                   tonemap->output(v_x, v_y, 2) / 65535.0f);

    yuv2rgb(tonemapOutputRgb, finalTonemap);
    
    //
    // Adjust hue & saturation
    //
    
    rgbToHsv(hsvInput, tonemapOutputRgb);

    shiftHues(saturationApplied, hsvInput, blueSaturation, greenSaturation, satParam);

    hsvToBgr(finalRgb, saturationApplied);

    // Finalize
    Expr b = 2.0f - pow(2.0f, contrast);
    Expr a = 2.0f - 2.0f * b;

    // Gamma correct
    Expr g = pow(v_i / 65535.0f, 1.0f / gamma);

    // Apply a piecewise quadratic contrast curve
    Expr h0 = select(g > 0.5f,
                     1.0f - (a*(1.0f-g)*(1.0f-g) + b*(1.0f-g)),
                     a*g*g + b*g);

    // Apply blacks/white point
    Expr h1 = (h0 - blacksParam) / whitePoint;

    gammaContrastLut(v_i) = cast<uint16_t>(clamp(h1*65535.0f+0.5f, 0.0f, 65535.0f));

    if(get_target().has_gpu_feature())
        gammaContrastLut.compute_root().gpu_tile(v_i, v_xi, 16);
    else
        gammaContrastLut.compute_root().vectorize(v_i, 8);

    // Gamma/contrast/black adjustment
    gammaCorrected(v_x, v_y, v_c) = gammaContrastLut(cast<uint16_t>(clamp(finalRgb(v_x, v_y, v_c) * 65535, 0, 65535))) / 65535.0f;
        
    //
    // Finalize output
    //
    
    output(v_x, v_y, v_c) = cast<uint8_t>(clamp(gammaCorrected(v_x, v_y, v_c) * 255 + 0.5f, 0, 255));

    // Output interleaved
    output
        .dim(0).set_stride(3)
        .dim(2).set_stride(1);
    
    if(get_target().has_gpu_feature())
        schedule_for_gpu();
    else
        schedule_for_cpu();
}

void PostProcessGenerator::schedule_for_gpu() {
    shadingMap0
        .reorder(v_x, v_y)
        .compute_at(shaded, v_x)
        .gpu_threads(v_x, v_y);

    shadingMap1
        .reorder(v_x, v_y)
        .compute_at(shaded, v_x)
        .gpu_threads(v_x, v_y);

    shadingMap2
        .reorder(v_x, v_y)
        .compute_at(shaded, v_x)
        .gpu_threads(v_x, v_y);

    shadingMap3
        .reorder(v_x, v_y)
        .compute_at(shaded, v_x)
        .gpu_threads(v_x, v_y);

    shaded
        .compute_root()
        .reorder(v_c, v_x, v_y)
        .unroll(v_c)
        .gpu_tile(v_x, v_y, v_xi, v_yi, 16, 32);

    bayerInput
        .compute_root()
        .reorder(v_x, v_y)
        .gpu_tile(v_x, v_y, v_xi, v_yi, 16, 32);

    demosaic->green
        .compute_root()
        .reorder(v_x, v_y)
        .gpu_tile(v_x, v_y, v_xi, v_yi, 16, 32);

    demosaic->redIntermediate
        .reorder( v_x, v_y)
        .compute_at(demosaic->output, v_x)
        .gpu_threads(v_x, v_y);

    demosaic->red
        .reorder( v_x, v_y)
        .compute_at(demosaic->output, v_x)
        .gpu_threads(v_x, v_y);

    demosaic->blueIntermediate
        .reorder( v_x, v_y)
        .compute_at(demosaic->output, v_x)
        .gpu_threads(v_x, v_y);

    demosaic->blue
        .reorder( v_x, v_y)
        .compute_at(demosaic->output, v_x)
        .gpu_threads(v_x, v_y);

    demosaic->output
        .compute_root()
        .unroll(v_c)
        .gpu_tile(v_x, v_y, v_xi, v_yi, 16, 32);

    adjustExposure
        .reorder(v_c, v_x, v_y)
        .compute_at(colorCorrectedYuv, v_x)
        .gpu_threads(v_x, v_y);

    colorCorrected
        .reorder(v_c, v_x, v_y)
        .compute_at(colorCorrectedYuv, v_x)
        .gpu_threads(v_x, v_y);

    colorCorrectedYuv
        .compute_root()
        .reorder(v_c, v_x, v_y)
        .gpu_tile(v_x, v_y, v_xi, v_yi, 16, 32);

    Yfiltered
        .compute_root()
        .reorder(v_x, v_y)
        .gpu_tile(v_x, v_y, v_xi, v_yi, 16, 32);

    uvDownsampled
        .compute_root()
        .bound(v_c, 0, 2)
        .reorder(v_c, v_x, v_y)
        .unroll(v_c)
        .gpu_tile(v_x, v_y, v_xi, v_yi, 16, 32);

    sharpened
        .reorder(v_x, v_y)
        .compute_at(output, v_x)
        .gpu_threads(v_x, v_y);

    tonemapOutputRgb
        .reorder(v_c, v_x, v_y)
        .compute_at(output, v_x)
        .unroll(v_c)
        .gpu_threads(v_x, v_y);

    gammaCorrected
        .reorder(v_c, v_x, v_y)
        .compute_at(output, v_x)
        .unroll(v_c)
        .gpu_threads(v_x, v_y);

    finalTonemap
        .reorder(v_c, v_x, v_y)
        .compute_at(output, v_x)
        .unroll(v_c)
        .gpu_threads(v_x, v_y);

    saturationApplied
        .reorder(v_c, v_x, v_y)
        .compute_at(output, v_x)
        .unroll(v_c)
        .gpu_threads(v_x, v_y);

    finalRgb
        .reorder(v_c, v_x, v_y)
        .compute_at(output, v_x)
        .unroll(v_c)
        .gpu_threads(v_x, v_y);

    output
        .compute_root()
        .bound(v_c, 0, 3)
        .reorder(v_c, v_x, v_y)
        .unroll(v_c)
        .gpu_tile(v_x, v_y, v_xi, v_yi, 12, 32);
}

void PostProcessGenerator::schedule_for_cpu() { 
    int vector_size_u8 = natural_vector_size<uint8_t>();
    int vector_size_u16 = natural_vector_size<uint16_t>();
    int vector_size_u32 = natural_vector_size<uint32_t>();
    int vector_size_f32 = natural_vector_size<float>();

    shadingMap0
        .reorder(v_x, v_y)
        .compute_at(combinedInput, v_yi)
        .store_at(combinedInput, v_yo)
        .vectorize(v_x, vector_size_u16);

    shadingMap1
        .reorder(v_x, v_y)
        .compute_at(combinedInput, v_yi)
        .store_at(combinedInput, v_yo)
        .vectorize(v_x, vector_size_u16);

    shadingMap2
        .reorder(v_x, v_y)
        .compute_at(combinedInput, v_yi)
        .store_at(combinedInput, v_yo)
        .vectorize(v_x, vector_size_u16);

    shadingMap3
        .reorder(v_x, v_y)
        .compute_at(combinedInput, v_yi)
        .store_at(combinedInput, v_yo)
        .vectorize(v_x, vector_size_u16);

    shaded
        .reorder(v_c, v_x, v_y)
        .unroll(v_c)
        .compute_at(combinedInput, v_yi)
        .store_at(combinedInput, v_yo)
        .vectorize(v_x, vector_size_u16);

    combinedInput
        .compute_root()
        .reorder(v_x, v_y)
        .split(v_y, v_yo, v_yi, 64)
        .parallel(v_yo)
        .vectorize(v_x, vector_size_u16);

    bayerInput
        .compute_root()
        .reorder(v_x, v_y)
        .split(v_y, v_yo, v_yi, 64)
        .parallel(v_yo)
        .vectorize(v_x, vector_size_u16);

    demosaic->green
        .compute_at(colorCorrectedYuv, subtile_idx)
        .store_at(colorCorrectedYuv, tile_idx)
        .vectorize(v_x, vector_size_u16);

    demosaic->redIntermediate
        .compute_at(colorCorrectedYuv, subtile_idx)
        .store_at(colorCorrectedYuv, tile_idx)
        .vectorize(v_x, vector_size_u16);

    demosaic->red
        .compute_at(colorCorrectedYuv, subtile_idx)
        .store_at(colorCorrectedYuv, tile_idx)
        .vectorize(v_x, vector_size_u16);

    demosaic->blueIntermediate
        .compute_at(colorCorrectedYuv, subtile_idx)
        .store_at(colorCorrectedYuv, tile_idx)
        .vectorize(v_x, vector_size_u16);

    demosaic->blue
        .compute_at(colorCorrectedYuv, subtile_idx)
        .store_at(colorCorrectedYuv, tile_idx)
        .vectorize(v_x, vector_size_u16);

    colorCorrected
        .compute_at(colorCorrectedYuv, subtile_idx)
        .store_at(colorCorrectedYuv, tile_idx)
        .vectorize(v_x, vector_size_u16);

    colorCorrectedYuv
        .compute_root()
        .reorder(v_c, v_x, v_y)
        .unroll(v_c)
        .tile(v_x, v_y, v_xo, v_yo, v_xi, v_yi, 64, 64)
        .fuse(v_xo, v_yo, tile_idx)
        .tile(v_xi, v_yi, v_xio, v_yio, v_xii, v_yii, 32, 32)
        .fuse(v_xio, v_yio, subtile_idx)
        .parallel(tile_idx)
        .vectorize(v_xii, vector_size_u16);

    Yfiltered
        .compute_root()
        .split(v_y, v_yo, v_yi, 32)
        .vectorize(v_x, vector_size_u16)
        .parallel(v_yo);

    uvDownsampled
        .compute_root()
        .bound(v_c, 0, 2)
        .reorder(v_c, v_x, v_y)
        .split(v_y, v_yo, v_yi, 32)
        .vectorize(v_x, vector_size_u16)
        .unroll(v_c)
        .parallel(v_yo);

    sharpened
        .compute_at(output, v_yi)
        .store_at(output, v_yo)
        .vectorize(v_x, vector_size_u32);

    finalTonemap
        .compute_at(output, v_yi)
        .store_at(output, v_yo)
        .vectorize(v_x, vector_size_u16);

    tonemapOutputRgb
        .compute_at(output, v_yi)
        .store_at(output, v_yo)
        .vectorize(v_x, vector_size_u16);

    gammaCorrected
        .compute_at(output, v_yi)
        .store_at(output, v_yo)
        .vectorize(v_x, vector_size_u16);

    saturationApplied
        .compute_at(output, v_yi)
        .store_at(output, v_yo)
        .vectorize(v_x, vector_size_f32);

    output
        .compute_root()
        .bound(v_c, 0, 3)
        .reorder(v_c, v_x, v_y)
        .split(v_y, v_yo, v_yi, 32)
        .parallel(v_yo)
        .unroll(v_c)
        .vectorize(v_x, vector_size_u8);
}

//

class PreviewGenerator : public Halide::Generator<PreviewGenerator>, public PostProcessBase {
public:
    GeneratorParam<int> rotation{"rotation", 0};
    GeneratorParam<int> tonemap_levels{"tonemap_levels", 8};
    GeneratorParam<int> detail_radius{"detail_radius", 15};
    GeneratorParam<int> downscaleFactor{"downscale_factor", 1};

    Input<Buffer<uint8_t>> input{"input", 1};

    Input<Buffer<float>> inshadingMap0{"inshadingMap0", 2 };
    Input<Buffer<float>> inshadingMap1{"inshadingMap1", 2 };
    Input<Buffer<float>> inshadingMap2{"inshadingMap2", 2 };
    Input<Buffer<float>> inshadingMap3{"inshadingMap3", 2 };
    
    Input<float[3]> asShotVector{"asShotVector"};
    Input<Buffer<float>> cameraToSrgb{"cameraToSrgb", 2};

    Input<int> width{"width"};
    Input<int> height{"height"};
    Input<int> stride{"stride"};
    Input<int> pixelFormat{"pixelFormat"};

    Input<int> sensorArrangement{"sensorArrangement"};
    
    Input<int16_t[4]> blackLevel{"blackLevel"};
    Input<int16_t> whiteLevel{"whiteLevel"};

    Input<float> gamma{"gamma"};
    Input<float> shadows{"shadows"};
    Input<float> whitePoint{"whitePoint"};
    Input<float> tonemapVariance{"tonemapVariance"};
    Input<float> blacks{"blacks"};
    Input<float> exposure{"exposure"};
    Input<float> contrast{"contrast"};
    Input<float> blueSaturation{"blueSaturation"};
    Input<float> saturation{"saturation"};
    Input<float> greenSaturation{"greenSaturation"};
    Input<float> detail{"detail"};

    Input<bool> flipped{"flipped"};

    Output<Buffer<uint8_t>> output{"output", 3};
    
    std::unique_ptr<TonemapGenerator> tonemap;
    
    void generate();
    void schedule_for_gpu();
    void schedule_for_cpu();
    
private:
    Func downscale(Func f, Func& downx, Expr factor);

private:
    Func inputRepeated;
    Func in[4];
    Func shadingMap[4];
    Func deinterleaved;
    Func downscaled;
    Func downscaledTemp;
    Func demosaicInput;
    Func downscaledInput;
    Func adjustExposure;
    Func yuvOutput;
    Func colorCorrected;
    Func colorCorrectedYuv;
    Func sharpenInputY;
    Func sharpened;
    Func finalTonemap;

    Func tonemapOutputRgb;
    Func gammaContrastLut;
    Func gammaCorrected;
    Func hsvInput;
    Func saturationApplied;
    Func finalRgb;

    std::unique_ptr<GuidedFilter> sharpenGf0;
};

Func PreviewGenerator::downscale(Func f, Func& downx, Expr factor) {
    Func in{"downscaleIn"}, downy{"downy"}, result{"downscaled"};
    RDom r(-factor/2, factor+1);

    in(v_x, v_y, v_c) = cast<int32_t>(f(v_x, v_y, v_c));

    downx(v_x, v_y, v_c) = fast_integer_divide(sum(in(v_x * factor + r.x, v_y, v_c)), cast<uint8_t>(factor + 1));
    downy(v_x, v_y, v_c) = fast_integer_divide(sum(downx(v_x, v_y * factor + r.x, v_c)), cast<uint8_t>(factor + 1));
    
    result(v_x, v_y, v_c) = cast<uint16_t>(downy(v_x, v_y, v_c));

    return result;
}

void PreviewGenerator::generate() {
    Expr shadowsParam  = shadows;
    Expr blacksParam   = blacks;
    Expr exposureParam = pow(2.0f, exposure);
    Expr satParam      = saturation;

    inputRepeated = BoundaryConditions::repeat_edge(input);

    // Deinterleave
    deinterleave(in[0], inputRepeated, 0, stride, pixelFormat);
    deinterleave(in[1], inputRepeated, 1, stride, pixelFormat);
    deinterleave(in[2], inputRepeated, 2, stride, pixelFormat);
    deinterleave(in[3], inputRepeated, 3, stride, pixelFormat);

    Expr w = width;
    Expr h = height;
    
    deinterleaved(v_x, v_y, v_c) =
        mux(v_c, { in[0](v_x, v_y), in[1](v_x, v_y), in[2](v_x, v_y), in[3](v_x, v_y) });

    downscaled = downscale(deinterleaved, downscaledTemp, downscaleFactor);

    // Shading map
    linearScale(shadingMap[0], inshadingMap0, inshadingMap0.width(), inshadingMap0.height(), w, h);
    linearScale(shadingMap[1], inshadingMap1, inshadingMap1.width(), inshadingMap1.height(), w, h);
    linearScale(shadingMap[2], inshadingMap2, inshadingMap2.width(), inshadingMap2.height(), w, h);
    linearScale(shadingMap[3], inshadingMap3, inshadingMap3.width(), inshadingMap3.height(), w, h);

    rearrange(demosaicInput, downscaled, sensorArrangement);

    Expr c0 = (demosaicInput(v_x, v_y, 0) - blackLevel[0]) / (cast<float>(whiteLevel - blackLevel[0])) * shadingMap[0](v_x, v_y);
    Expr c1 = (demosaicInput(v_x, v_y, 1) - blackLevel[1]) / (cast<float>(whiteLevel - blackLevel[1])) * shadingMap[1](v_x, v_y);
    Expr c2 = (demosaicInput(v_x, v_y, 2) - blackLevel[2]) / (cast<float>(whiteLevel - blackLevel[2])) * shadingMap[2](v_x, v_y);
    Expr c3 = (demosaicInput(v_x, v_y, 3) - blackLevel[3]) / (cast<float>(whiteLevel - blackLevel[3])) * shadingMap[3](v_x, v_y);
    
    downscaledInput(v_x, v_y, v_c) = select(v_c == 0,  clamp( c0,               0.0f, asShotVector[0] ),
                                            v_c == 1,  clamp( (c1 + c2) / 2,    0.0f, asShotVector[1] ),
                                                       clamp( c3,               0.0f, asShotVector[2] ));

    // Transform to SRGB space
    transform(colorCorrected, downscaledInput, cameraToSrgb);

    // Adjust exposure
    adjustExposure(v_x, v_y, v_c) = clamp(exposureParam * colorCorrected(v_x, v_y, v_c), 0.0f, 1.0f);

    // Move to YUV space
    rgb2yuv(yuvOutput, adjustExposure);

    colorCorrectedYuv(v_x, v_y, v_c) = cast<uint16_t>(clamp(yuvOutput(v_x, v_y, v_c) * 65535.0f + 0.5f, 0, 65535));

    // Tonemap
    tonemap = create<TonemapGenerator>();

    tonemap->output_type.set(UInt(16));
    tonemap->tonemap_levels.set(tonemap_levels);

    tonemap->apply(colorCorrectedYuv, width, height, tonemapVariance, gamma, shadowsParam);
    
    //
    // Sharpen
    //

    sharpenInputY(v_x, v_y) = tonemap->output(v_x, v_y, 0);

    sharpenGf0 = create<GuidedFilter>();
    sharpenGf0->radius.set(detail_radius);
    sharpenGf0->apply(sharpenInputY, 0.1f*0.1f * 65535*65535);
    
    Func gaussianDiff0;
    
    gaussianDiff0(v_x, v_y) = cast<int32_t>(sharpenInputY(v_x, v_y)) - cast<int32_t>(sharpenGf0->output(v_x, v_y));
    
    sharpened(v_x, v_y) =
        saturating_cast<int32_t>(
            sharpenGf0->output(v_x, v_y) +
            detail*gaussianDiff0(v_x, v_y)
        );

    // Back to RGB
    finalTonemap(v_x, v_y, v_c) = select(v_c == 0, sharpened(v_x, v_y) / 65535.0f,
                                         v_c == 1, tonemap->output(v_x, v_y, 1) / 65535.0f,
                                                   tonemap->output(v_x, v_y, 2) / 65535.0f);

    // Back to RGB
    yuv2rgb(tonemapOutputRgb, finalTonemap);
    
    // Finalize
    Expr b = 2.0f - pow(2.0f, contrast);
    Expr a = 2.0f - 2.0f * b;

    // Gamma correct
    Expr g = pow(v_i / 255.0f, 1.0f / gamma);

    // Apply a piecewise quadratic contrast curve
    Expr h0 = select(g > 0.5f,
                     1.0f - (a*(1.0f-g)*(1.0f-g) + b*(1.0f-g)),
                     a*g*g + b*g);

    // Apply blacks/white point
    Expr h1 = (h0 - blacksParam) / whitePoint;

    gammaContrastLut(v_i) = cast<uint8_t>(clamp(h1*255.0f+0.5f, 0.0f, 255.0f));

    if(get_target().has_gpu_feature())
        gammaContrastLut.compute_root().gpu_tile(v_i, v_xi, 16);
    else
        gammaContrastLut.compute_root().vectorize(v_i, 8);

    // Gamma/contrast/black adjustment
    gammaCorrected(v_x, v_y, v_c) = gammaContrastLut(cast<uint8_t>(clamp(tonemapOutputRgb(v_x, v_y, v_c) * 255, 0, 255)));
    
    //
    // Adjust saturation
    //

    Func gammaCorrected32;
    
    gammaCorrected32(v_x, v_y, v_c) = gammaCorrected(v_x, v_y, v_c) / 255.0f;

    rgbToHsv(hsvInput, gammaCorrected32);

    shiftHues(saturationApplied, hsvInput, blueSaturation, greenSaturation, satParam);

    hsvToBgr(finalRgb, saturationApplied);
    
    //
    // Finalize output
    //

    Expr X, Y;

    switch(rotation) {
        case 90:
            X = width - v_y;
            Y = select(flipped, height - v_x, v_x);
            break;

        case -90:
            X = v_y;
            Y = select(flipped, v_x, height - v_x);
            break;

        case 180:
            X = v_x;
            Y = height - v_y;
            break;

        default:
        case 0:
            X = select(flipped, width - v_x, v_x);
            Y = v_y;
            break;
    }

    output(v_x, v_y, v_c) = cast<uint8_t>(clamp(
        select( v_c == 0, finalRgb(X, Y, 2) * 255 + 0.5f,
                v_c == 1, finalRgb(X, Y, 1) * 255 + 0.5f,
                v_c == 2, finalRgb(X, Y, 0) * 255 + 0.5f,
                          255), 0, 255));

    // Output interleaved
    output
        .dim(0).set_stride(4)
        .dim(2).set_stride(1);

    
    if(get_target().has_gpu_feature())
        schedule_for_gpu();
    else
        schedule_for_cpu();
}

void PreviewGenerator::schedule_for_gpu() {
    downscaledInput
        .reorder(v_c, v_x, v_y)
        .unroll(v_c)
        .compute_at(colorCorrectedYuv, v_x)
        .gpu_threads(v_x, v_y);

    adjustExposure
        .reorder(v_c, v_x, v_y)
        .unroll(v_c)
        .compute_at(colorCorrectedYuv, v_x)
        .gpu_threads(v_x, v_y);

    colorCorrected
        .reorder(v_c, v_x, v_y)
        .unroll(v_c)
        .compute_at(colorCorrectedYuv, v_x)
        .gpu_threads(v_x, v_y);

    colorCorrectedYuv
        .compute_root()
        .reorder(v_c, v_x, v_y)
        .unroll(v_c)
        .gpu_tile(v_x, v_y, v_xi, v_yi, 8, 16);

    tonemapOutputRgb
        .reorder(v_c, v_x, v_y)
        .compute_at(gammaCorrected, v_x)
        .unroll(v_c)
        .gpu_threads(v_x, v_y);

    finalTonemap
        .reorder(v_c, v_x, v_y)
        .compute_at(gammaCorrected, v_x)
        .unroll(v_c)
        .gpu_threads(v_x, v_y);

    gammaCorrected
        .compute_root()
        .reorder(v_c, v_x, v_y)
        .unroll(v_c)
        .gpu_tile(v_x, v_y, v_xi, v_yi, 8, 16);

    output
        .compute_root()
        .bound(v_c, 0, 4)
        .reorder(v_c, v_x, v_y)
        .unroll(v_c)
        .gpu_tile(v_x, v_y, v_xi, v_yi, 8, 16);
}

void PreviewGenerator::schedule_for_cpu() {
    int vector_size_u8 = natural_vector_size<uint8_t>();
    int vector_size_u16 = natural_vector_size<uint16_t>();    

    for(int c = 0; c < 4; c++) {
        shadingMap[c]
            .reorder(v_x, v_y)
            .compute_at(downscaledInput, v_yi)
            .store_at(downscaledInput, v_yo)
            .vectorize(v_x, vector_size_u16);
    }

    downscaledTemp
        .reorder(v_c, v_x, v_y)
        .unroll(v_c)
        .compute_at(downscaledInput, v_yi)
        .store_at(downscaledInput, v_yo)
        .vectorize(v_x, vector_size_u16);

    downscaled
        .reorder(v_c, v_x, v_y)
        .unroll(v_c)
        .compute_at(downscaledInput, v_yi)
        .store_at(downscaledInput, v_yo)
        .vectorize(v_x, vector_size_u16);

    demosaicInput
        .reorder(v_c, v_x, v_y)
        .unroll(v_c)
        .compute_at(downscaledInput, v_yi)
        .store_at(downscaledInput, v_yo)
        .vectorize(v_x, vector_size_u16);

    downscaledInput
        .compute_root()
        .reorder(v_c, v_x, v_y)
        .unroll(v_c)
        .split(v_y, v_yo, v_yi, 32)
        .parallel(v_yo)
        .vectorize(v_x, vector_size_u16);

    colorCorrectedYuv
        .compute_root()
        .reorder(v_c, v_x, v_y)
        .unroll(v_c)
        .split(v_y, v_yo, v_yi, 32)
        .parallel(v_yo)
        .vectorize(v_x, vector_size_u16);

    output
        .compute_root()
        .bound(v_c, 0, 4)
        .reorder(v_c, v_x, v_y)
        .tile(v_x, v_y, v_xo, v_yo, v_xi, v_yi, 64, 64)
        .fuse(v_xo, v_yo, tile_idx)
        .parallel(tile_idx)
        .unroll(v_c)
        .vectorize(v_xi, vector_size_u8);
}

//

class DeinterleaveRawGenerator : public Halide::Generator<DeinterleaveRawGenerator>, public PostProcessBase {
public:
    Input<Buffer<uint8_t>> input{"input", 1};
    Input<int> stride{"stride"};
    Input<int> pixelFormat{"pixelFormat"};
    Input<int> sensorArrangement{"sensorArrangement"};
    
    Input<int> width{"width"};
    Input<int> height{"height"};

    Input<int> offsetX{"offsetX"};
    Input<int> offsetY{"offsetY"};

    Input<int>    whiteLevel{"whiteLevel"};
    Input<int[4]> blackLevel{"blackLevel"};
    Input<float>  scale{"scale"};

    Output<Buffer<uint16_t>> output{"output", 3};
    Output<Buffer<uint8_t>> preview{"preview", 2};

    void generate();
    void schedule_for_cpu();
};

void DeinterleaveRawGenerator::generate() {
    Func channels[4];
    Func deinterleaved;

    // Deinterleave
    deinterleave(channels[0], input, 0, stride, pixelFormat);
    deinterleave(channels[1], input, 1, stride, pixelFormat);
    deinterleave(channels[2], input, 2, stride, pixelFormat);
    deinterleave(channels[3], input, 3, stride, pixelFormat);

    deinterleaved(v_x, v_y, v_c) = select(
        v_c == 0, channels[0](v_x, v_y),
        v_c == 1, channels[1](v_x, v_y),
        v_c == 2, channels[2](v_x, v_y),
                  channels[3](v_x, v_y));

    Func clamped = BoundaryConditions::mirror_image(deinterleaved, { {0, width - 1}, {0, height - 1}, {0, 4} });
    
    // Gamma correct preview
    Func gammaLut;
    
    gammaLut(v_i) = cast<uint8_t>(clamp(pow(v_i / 255.0f, 1.0f / 2.2f) * 255, 0, 255));    

    if(!get_auto_schedule())
        gammaLut.compute_root();

    Expr x = v_x - offsetX;
    Expr y = v_y - offsetY;

    output(v_x, v_y, v_c) = clamped(x, y, v_c);

    Expr p =
        select( sensorArrangement == static_cast<int>(SensorArrangement::RGGB),
                    ((cast<float>(clamped(v_x, v_y, 1)) + cast<float>(clamped(v_x, v_y, 2))) / 2.0f - blackLevel[1]) / (whiteLevel - blackLevel[1]),

                sensorArrangement == static_cast<int>(SensorArrangement::GRBG),
                    ((cast<float>(clamped(v_x, v_y, 0)) + cast<float>(clamped(v_x, v_y, 3))) / 2.0f - blackLevel[0]) / (whiteLevel - blackLevel[0]),

                sensorArrangement == static_cast<int>(SensorArrangement::GBRG),
                    ((cast<float>(clamped(v_x, v_y, 0)) + cast<float>(clamped(v_x, v_y, 3))) / 2.0f - blackLevel[0]) / (whiteLevel - blackLevel[0]),

                    // BGGR
                    ((cast<float>(clamped(v_x, v_y, 1)) + cast<float>(clamped(v_x, v_y, 2))) / 2.0f - blackLevel[1]) / (whiteLevel - blackLevel[1]) );

    preview(v_x, v_y) =  gammaLut(cast<uint8_t>(clamp(p * scale * 255.0f + 0.5f, 0, 255)));

    if(!get_auto_schedule()) {
        schedule_for_cpu();
    }
 }

void DeinterleaveRawGenerator::schedule_for_cpu() {    
    output
        .compute_root()
        .reorder(v_c, v_x, v_y)
        .split(v_y, v_yo, v_yi, 16)
        .vectorize(v_x, 16)
        .parallel(v_yo)
        .unroll(v_c, 4);

    preview
        .compute_root()
        .split(v_y, v_yo, v_yi, 16)
        .vectorize(v_x, 16)
        .parallel(v_yo);
}


//

class MeasureImageGenerator : public Halide::Generator<MeasureImageGenerator>, public PostProcessBase {
public:
    Input<Buffer<uint8_t>> input{"input", 1};
    Input<int> stride{"stride"};
    Input<int> pixelFormat{"pixelFormat"};
    
    Input<int> width{"width"};
    Input<int> height{"height"};

    Input<int> downscaleFactor{"downscaleFactor"};    

    Input<int[4]> blackLevel{"blackLevel"};
    Input<int> whiteLevel{"whiteLevel"};

    Input<float[4]> colorCorrectionGains{"colorCorrectionGains"};    
    Input<Buffer<float>[4]> shadingMap{"shadingMap", 2 };

    Input<float[3]> asShotVector{"asShotVector"};
    Input<int> sensorArrangement{"sensorArrangement"};

    Output<Buffer<uint32_t>> histogram{"histogram", 2};

    void generate();
};

void MeasureImageGenerator::generate() {
    Func inputRepeated;
    Func channels[4];
    Func downscaled[4];
    Func scaledShadingMap[4];

    // Deinterleave
    inputRepeated = BoundaryConditions::repeat_edge(input);

    deinterleave(channels[0], inputRepeated, 0, stride, pixelFormat);
    deinterleave(channels[1], inputRepeated, 1, stride, pixelFormat);
    deinterleave(channels[2], inputRepeated, 2, stride, pixelFormat);
    deinterleave(channels[3], inputRepeated, 3, stride, pixelFormat);
    
    downscaled[0](v_x, v_y) = channels[0](v_x*downscaleFactor, v_y*downscaleFactor);
    downscaled[1](v_x, v_y) = channels[1](v_x*downscaleFactor, v_y*downscaleFactor);
    downscaled[2](v_x, v_y) = channels[2](v_x*downscaleFactor, v_y*downscaleFactor);
    downscaled[3](v_x, v_y) = channels[3](v_x*downscaleFactor, v_y*downscaleFactor);

    Expr w = width  / downscaleFactor;
    Expr h = height / downscaleFactor;

    // Shading map
    for(int c = 0; c < 4; c++)
        linearScale(scaledShadingMap[c], shadingMap[c], shadingMap[c].width(), shadingMap[c].height(), w, h);

    Func demosaicInput("demosaicInput");
    Func shadingInput("shadingInput");

    rearrange(demosaicInput, downscaled[0], downscaled[1], downscaled[2], downscaled[3], sensorArrangement);

    Expr c0 = (demosaicInput(v_x, v_y, 0) - blackLevel[0]) / (cast<float>(whiteLevel - blackLevel[0])) * scaledShadingMap[0](v_x, v_y) * colorCorrectionGains[0];
    Expr c1 = (demosaicInput(v_x, v_y, 1) - blackLevel[1]) / (cast<float>(whiteLevel - blackLevel[1])) * scaledShadingMap[1](v_x, v_y) * colorCorrectionGains[1];
    Expr c2 = (demosaicInput(v_x, v_y, 2) - blackLevel[2]) / (cast<float>(whiteLevel - blackLevel[2])) * scaledShadingMap[2](v_x, v_y) * colorCorrectionGains[2];
    Expr c3 = (demosaicInput(v_x, v_y, 3) - blackLevel[3]) / (cast<float>(whiteLevel - blackLevel[3])) * scaledShadingMap[3](v_x, v_y) * colorCorrectionGains[3];
        
    Func result, result8u;

    result(v_x, v_y, v_c) = select(v_c == 0,  clamp( c0,               0.0f, asShotVector[0] ),
                                   v_c == 1,  clamp( (c1 + c2) / 2,    0.0f, asShotVector[1] ),
                                              clamp( c3,               0.0f, asShotVector[2] ));

    result8u(v_x, v_y, v_c) = cast<uint8_t>(clamp(result(v_x, v_y, v_c) * 255 + 0.5f, 0, 255));

    RDom r(0, w, 0, h);

    histogram(v_i, v_c) = cast<uint32_t>(0);
    histogram(result8u(r.x, r.y, v_c), v_c) += cast<uint32_t>(1);

    // Schedule
    result8u
        .compute_root()
        .reorder(v_c, v_x, v_y)
        .unroll(v_c, 3)
        .parallel(v_y, 8)
        .vectorize(v_x, 8);

    histogram
        .compute_root()
        .parallel(v_c)
        .vectorize(v_i, 128);
}

//////////////

class GenerateEdgesGenerator : public Halide::Generator<GenerateEdgesGenerator>, public PostProcessBase {
public:
    Input<Buffer<uint8_t>> input{"input", 1};
    Input<int> stride{"stride"};
    Input<int> pixelFormat{"pixelFormat"};
    
    Input<int> width{"width"};
    Input<int> height{"height"};

    Output<Buffer<uint16_t>> output{"output", 2};

    void generate();
};

void GenerateEdgesGenerator::generate() {
    Func channel, channel32;

    deinterleave(channel, input, 0, stride, pixelFormat);
    
    channel32(v_x, v_y) = cast<int32_t>(channel(v_x, v_y));

    Func bounded = BoundaryConditions::repeat_edge(channel32, { { 0, width - 1}, {0, height - 1} });

    Func sobel_x_avg{"sobel_x_avg"}, sobel_y_avg{"sobel_y_avg"};
    Func sobel_x{"sobel_x"}, sobel_y{"sobel_y"};

    sobel_x_avg(v_x, v_y) = bounded(v_x - 1, v_y) + 2 * bounded(v_x, v_y) + bounded(v_x + 1, v_y);
    sobel_x(v_x, v_y) = absd(sobel_x_avg(v_x, v_y - 1), sobel_x_avg(v_x, v_y + 1));

    sobel_y_avg(v_x, v_y) = bounded(v_x, v_y - 1) + 2 * bounded(v_x, v_y) + bounded(v_x, v_y + 1);
    sobel_y(v_x, v_y) = absd(sobel_y_avg(v_x - 1, v_y), sobel_y_avg(v_x + 1, v_y));
    
    output(v_x, v_y) = cast<uint16_t>(clamp(sobel_x(v_x, v_y) + sobel_y(v_x, v_y), 0, 65535));

    channel32
        .compute_at(output, v_yi)
        .store_at(output, v_yo)
        .vectorize(v_x, 8);

    output.compute_root()
        .vectorize(v_x, 8)
        .split(v_y, v_yo, v_yi, 32)
        .parallel(v_yo);
}

HALIDE_REGISTER_GENERATOR(GenerateEdgesGenerator, generate_edges_generator)
HALIDE_REGISTER_GENERATOR(MeasureImageGenerator, measure_image_generator)
HALIDE_REGISTER_GENERATOR(DeinterleaveRawGenerator, deinterleave_raw_generator)
HALIDE_REGISTER_GENERATOR(PostProcessGenerator, postprocess_generator)
HALIDE_REGISTER_GENERATOR(PreviewGenerator, preview_generator)


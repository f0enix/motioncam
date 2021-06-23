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

    if(!auto_schedule) {
        if(get_target().has_gpu_feature())
            schedule_for_gpu();
        else
            schedule_for_cpu();
    }
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
    
    Func redIntermediate{"redIntermediate"};
    Func red{"red"};
    Func greenIntermediate{"greenIntermediate"};
    Func green{"green"};
    Func blueIntermediate{"blueIntermediate"};
    Func blue{"blue"};

    void generate();
    void schedule();
    
    void cmpSwap(Expr& a, Expr& b);

    void calculateGreen(Func& output, Func input);
    void calculateGreen2(Func& output, Func input);

    void calculateRed(Func& output, Func input, Func green);
    void calculateBlue(Func& output, Func input, Func green);

    void weightedMedianFilter(Func& output, Func input);
};

void Demosaic::weightedMedianFilter(Func& output, Func input) {

    Expr p0 = input(v_x,   v_y);
    Expr p1 = input(v_x,   v_y);
    Expr p2 = input(v_x,   v_y);
    Expr p3 = input(v_x,   v_y);

    Expr p4 = input(v_x-1, v_y);
    Expr p5 = input(v_x+1, v_y);
    Expr p6 = input(v_x, v_y-1);
    Expr p7 = input(v_x, v_y+1);
    Expr p8 = input(v_x-1, v_y-1);
    Expr p9 = input(v_x-1, v_y+1);
    Expr p10 = input(v_x+1, v_y-1);
    Expr p11 = input(v_x+1, v_y+1);
    
    cmpSwap(p1, p2);
    cmpSwap(p0, p2);
    cmpSwap(p0, p1);
    cmpSwap(p4, p5);
    cmpSwap(p3, p5);
    cmpSwap(p3, p4);
    cmpSwap(p0, p3);
    cmpSwap(p1, p4);
    cmpSwap(p2, p5);
    cmpSwap(p2, p4);
    cmpSwap(p1, p3);
    cmpSwap(p2, p3);
    cmpSwap(p7, p8);
    cmpSwap(p6, p8);
    cmpSwap(p6, p7);
    cmpSwap(p10,p11);
    cmpSwap(p9, p11);
    cmpSwap(p9, p10);
    cmpSwap(p6, p9);
    cmpSwap(p7, p10);
    cmpSwap(p8, p11);
    cmpSwap(p8, p10);
    cmpSwap(p7, p9);
    cmpSwap(p8, p9);
    cmpSwap(p0, p6);
    cmpSwap(p1, p7);
    cmpSwap(p2, p8);
    cmpSwap(p2, p7);
    cmpSwap(p1, p6);
    cmpSwap(p2, p6);
    cmpSwap(p3, p9);
    cmpSwap(p4, p10);
    cmpSwap(p5, p11);
    cmpSwap(p5, p10);
    cmpSwap(p4, p9);
    cmpSwap(p5, p9);
    cmpSwap(p3, p6);
    cmpSwap(p4, p7);
    cmpSwap(p5, p8);
    cmpSwap(p5, p7);
    cmpSwap(p4, p6);
    cmpSwap(p5, p6);

    output(v_x, v_y) = cast<int16_t>((cast<int32_t>(p5) + cast<int32_t>(p6)) / 2);
}

void Demosaic::cmpSwap(Expr& a, Expr& b) {
    Expr tmp = min(a, b);
    b = max(a, b);
    a = tmp;
}

void Demosaic::calculateGreen2(Func& output, Func input) {
    const int M = 1;
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

    greenIntermediate(v_x, v_y) = select(
        ((v_x + v_y) & 1) == 1,
            input(v_x, v_y),
            saturating_cast<int16_t>(interp + 0.5f));

    Func filtered{"greenFiltered"};

    weightedMedianFilter(output, greenIntermediate);    
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
    
    greenIntermediate(v_x, v_y) = select(((v_x + v_y) & 1) == 1, input(v_x, v_y), cast<int16_t>(interp + 0.5f));

    Func filtered{"greenFiltered"};

    weightedMedianFilter(output, greenIntermediate);
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

    Func filtered{"redFiltered"};

    weightedMedianFilter(filtered, redIntermediate);

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

    Func filtered{"blueFiltered"};

    weightedMedianFilter(filtered, blueIntermediate);

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

    Input<Func> input{"input", 2 };
    Output<Func> output{ "tonemapOutput", 2 };

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
         (
          1 * expanded(v_x - 1, v_y, v_c) +
          2 * expanded(v_x,     v_y, v_c) +
          1 * expanded(v_x + 1, v_y, v_c)
          ), 4);

    blurY(v_x, v_y, v_c) = fast_integer_divide(
         (
          1 * blurX(v_x, v_y - 1, v_c) +
          2 * blurX(v_x, v_y,     v_c) +
          1 * blurX(v_x, v_y + 1, v_c)
          ), 4);

    intermediate = blurX;
    output(v_x, v_y, v_c) = 4 * blurY(v_x, v_y, v_c);
}

void TonemapGenerator::pyramidDown(Func& output, Func& intermediate, Func input) {
    Func blurX, blurY;

    blurX(v_x, v_y, v_c) = fast_integer_divide(
         (
          1 * input(v_x - 1, v_y, v_c) +
          2 * input(v_x,     v_y, v_c) +
          1 * input(v_x + 1, v_y, v_c)
          ), 4);

    blurY(v_x, v_y, v_c) = fast_integer_divide(
         (
          1 * blurX(v_x, v_y - 1, v_c) +
          2 * blurX(v_x, v_y,     v_c) +
          1 * blurX(v_x, v_y + 1, v_c)
          ), 4);

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

    if(!auto_schedule) {
        if(get_target().has_gpu_feature()) {
            gammaLut.compute_root().gpu_tile(v_i, v_xi, 16);
            inverseGammaLut.compute_root().gpu_tile(v_i, v_xi, 16);
        }
        else {
            gammaLut.compute_root().vectorize(v_i, 8);
            inverseGammaLut.compute_root().vectorize(v_i, 8);
        }
    }

    // Create two exposures
    Func exposures, weightsLut, weights, weightsNormalized;
    
    Expr ia = input(v_x, v_y);
    Expr ib = cast(output_type, clamp(cast<float>(input(v_x, v_y)) * gain, 0.0f, type_max));

    exposures(v_x, v_y, v_c) = select(v_c == 0, cast<int32_t>(gammaLut(ia)),
                                                cast<int32_t>(gammaLut(ib)));

    // Create weights LUT based on well exposed pixels
    Expr wa = v_i / cast<float>(type_max) - 0.5f;
    Expr wb = -pow(wa, 2) / (2 * variance * variance);
    
    weightsLut(v_i) = cast<int16_t>(clamp(exp(wb) * 32767, -32767, 32767));
    
    if(!auto_schedule) {
        if(get_target().has_gpu_feature()) {
            weightsLut.compute_root().gpu_tile(v_i, v_xi, 16);
        }
        else {
            weightsLut.compute_root().vectorize(v_i, 8);
        }
    }

    weights(v_x, v_y, v_c) = weightsLut(cast<uint16_t>(exposures(v_x, v_y, v_c))) / 32767.0f;
    weightsNormalized(v_x, v_y, v_c) = weights(v_x, v_y, v_c) / (1e-12f + weights(v_x, v_y, 0) + weights(v_x, v_y, 1));

    // Create pyramid input
    tonemapPyramid = buildPyramid(exposures, tonemap_levels);
    weightsPyramid = buildPyramid(weightsNormalized, tonemap_levels);

    if(!auto_schedule) {
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
        if(!auto_schedule) {
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

        if(!auto_schedule) {
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
        }

        outputPyramid.push_back(outputLvl);
    }

    // Inverse gamma correct tonemapped result
    output(v_x, v_y) = inverseGammaLut(cast(output_type, clamp(outputPyramid[tonemap_levels - 1](v_x, v_y, 0), 0, type_max)));
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

    void blur(Func& output, Func& outputTmp, Func input);
    void blur2(Func& output, Func& outputTmp, Func input);
    void blur3(Func& output, Func& outputTmp, Func input);

    Func downsample(Func f, Func& temp);
    Func upsample(Func f, Func& temp);

    void rgb2yuv(Func& output, Func input);
    void yuv2rgb(Func& output, Func input);

    void cmpSwap(Expr& a, Expr& b);

    void rgbToHsv(Func& output, Func input);
    void hsvToBgr(Func& output, Func input);

    void shiftHues(
        Func& output, Func hsvInput, Expr blues, Expr greens, Expr saturation);

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


void PostProcessBase::blur(Func& output, Func& outputTmp, Func input) {
    Func in32{"blur_in32"};

    in32(v_x, v_y) = cast<int32_t>(input(v_x, v_y));
    
    outputTmp(v_x, v_y) = (
        1 * in32(v_x - 1, v_y) +
        2 * in32(v_x    , v_y) +
        1 * in32(v_x + 1, v_y)
    ) / 4;

    output(v_x, v_y) =
        cast<uint16_t> (
            (
              1 * outputTmp(v_x, v_y - 1) +
              2 * outputTmp(v_x, v_y)     +
              1 * outputTmp(v_x, v_y + 1)             
            ) / 4
        );
}

void PostProcessBase::blur2(Func& output, Func& outputTmp, Func input) {
    Func in32{"blur2_in32"};

    in32(v_x, v_y) = cast<int32_t>(input(v_x, v_y));
    
    outputTmp(v_x, v_y) = (
        1 * in32(v_x - 2, v_y) +
        4 * in32(v_x - 1, v_y) +
        6 * in32(v_x,     v_y) +
        4 * in32(v_x + 1, v_y) +
        1 * in32(v_x + 2, v_y)
    ) / 16;

    output(v_x, v_y) =
        cast<uint16_t> (
            (
              1 * outputTmp(v_x, v_y - 2) +
              4 * outputTmp(v_x, v_y - 1) +
              6 * outputTmp(v_x, v_y)     +
              4 * outputTmp(v_x, v_y + 1) +
              1 * outputTmp(v_x, v_y + 2)             
            ) / 16
        );
}

void PostProcessBase::blur3(Func& output, Func& outputTmp, Func input) {
    Func in32{"blur3_in32"};

    in32(v_x, v_y) = cast<int32_t>(input(v_x, v_y));

    outputTmp(v_x, v_y) = (
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
            1  * outputTmp(v_x, v_y - 4) +
            8  * outputTmp(v_x, v_y - 3) +
            28 * outputTmp(v_x, v_y - 2) +
            56 * outputTmp(v_x, v_y - 1) +
            70 * outputTmp(v_x, v_y)     +
            56 * outputTmp(v_x, v_y + 1) +
            28 * outputTmp(v_x, v_y + 2) +
            8  * outputTmp(v_x, v_y + 3) +
            1  * outputTmp(v_x, v_y + 4)
            ) / 256
        );
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
    
    Expr fx = max(0.0f, (v_x + 0.5f) * fast_inverse(scaleX) - 0.5f);
    Expr fy = max(0.0f, (v_y + 0.5f) * fast_inverse(scaleY) - 0.5f);
    
    Expr x = cast<int16_t>(fx);
    Expr y = cast<int16_t>(fy);
    
    Expr a = fx - x;
    Expr b = fy - y;

    Expr x0 = clamp(x, 0, cast<int16_t>(fromWidth) - 1);
    Expr y0 = clamp(y, 0, cast<int16_t>(fromHeight) - 1);

    Expr x1 = clamp(x + 1, 0, cast<int16_t>(fromWidth) - 1);
    Expr y1 = clamp(y + 1, 0, cast<int16_t>(fromHeight) - 1);
    
    Expr p0 = lerp(cast<float>(image(x0, y0)), cast<float>(image(x1, y0)), a);
    Expr p1 = lerp(cast<float>(image(x0, y1)), cast<float>(image(x1, y1)), a);

    result(v_x, v_y) = lerp(p0, p1, b);
}

void PostProcessBase::shiftHues(
    Func& output, Func hsvInput, Expr blues, Expr greens, Expr saturation)
{
    Expr H = hsvInput(v_x, v_y, 0);
    Expr S = hsvInput(v_x, v_y, 1);

    Expr blueWeight   = exp(-(H - 210)*(H - 210) / 800);
    Expr greenWeight  = exp(-(H - 90)*(H - 90) / 800);

    output(v_x, v_y, v_c) =
        select(v_c == 0, H + blues*blueWeight + greens*greenWeight,
               v_c == 1, clamp(S * saturation, 0.0f, 1.0f),
                         hsvInput(v_x, v_y, v_c));
}

//

class PostProcessGenerator : public Halide::Generator<PostProcessGenerator>, public PostProcessBase {
public:
    Input<Buffer<uint16_t>> in0{"in0", 2 };
    Input<Buffer<uint16_t>> in1{"in1", 2 };
    Input<Buffer<uint16_t>> in2{"in2", 2 };
    Input<Buffer<uint16_t>> in3{"in3", 2 };

    Input<Buffer<uint16_t>> hdrInput{"hdrInput", 3 };
    Input<Buffer<uint8_t>> hdrMask{"hdrMask", 2 };
    Input<float> hdrScale{"hdrScale"};

    Input<float[3]> asShotVector{"asShotVector"};
    Input<Buffer<float>> cameraToPcs{"cameraToPcs", 2};
    Input<Buffer<float>> pcsToSrgb{"pcsToSrgb", 2};

    Input<Buffer<float>> inShadingMap0{"inShadingMap0", 2 };
    Input<Buffer<float>> inShadingMap1{"inShadingMap1", 2 };
    Input<Buffer<float>> inShadingMap2{"inShadingMap2", 2 };
    Input<Buffer<float>> inShadingMap3{"inShadingMap3", 2 };

    Input<uint16_t> range{"range"};
    Input<int> sensorArrangement{"sensorArrangement"};
    
    Input<float> gamma{"gamma"};
    Input<float> shadows{"shadows"};
    Input<float> tonemapVariance{"tonemapVariance"};
    Input<float> blacks{"blacks"};
    Input<float> exposure{"exposure"};
    Input<float> whitePoint{"whitePoint"};
    Input<float> contrast{"contrast"};
    Input<float> blues{"blues"};
    Input<float> greens{"greens"};
    Input<float> saturation{"saturation"};
    Input<float> sharpen0{"sharpen0"};
    Input<float> sharpen1{"sharpen1"};
    Input<float> sharpenThreshold{"sharpenThreshold"};    
    Input<float> chromaEps{"chromaEps"};

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
    Func hdrMerged{"hdrMerged"};
    Func colorCorrectedYuv{"colorCorrectedYuv"};
    Func Yfiltered{"Yfiltered"}, Udownsampled{"Udownsampled"}, Vdownsampled{"Vdownsampled"};
    Func Ublended{"Ublended"}, Vblended{"Vblended"};
    Func uvDownsampled{"uvDownsampled"};
    Func tonemapIn{"tonemapIn"};
    Func tonemapOutputRgb{"tonemapOutputRgb"};
    Func gammaCorrected{"gammaCorrected"};
    Func sharpened{"sharpened"};
    Func chromaDenoiseInputU{"chromaDenoiseInputU"}, chromaDenoiseInputV{"chromaDenoiseInputV"};
    Func finalTonemap{"finalTonemap"};
    Func hsvInput{"hsvInput"};
    Func hsvFixed{"hsvFixed"};
    Func saturationValue{"saturationValue"}, saturationFiltered{"saturationFiltered"};
    Func saturationApplied{"saturationApplied"};
    Func finalRgb{"finalRgb"};
    Func gammaLut{"gammaLut"};
    Func blurOutput{"blurOutput"};
    Func blurOutputTmp{"blurOutputTmp"};
    Func blurOutput2{"blurOutput2"};
    Func blurOutput2Tmp{"blurOutput2Tmp"};

    Func shaded{"shaded"};
    Func shadingMap0{"shadingMap0"}, shadingMap1{"shadingMap1"}, shadingMap2{"shadingMap2"}, shadingMap3{"shadingMap3"};
    Func shadingMapArranged{"shadingMapArranged"};

    std::unique_ptr<Demosaic> demosaic;
    std::unique_ptr<TonemapGenerator> tonemap;
    
    void generate();
    void schedule_for_gpu();
    void schedule_for_cpu();
    
private:
    void sharpen(Func sharpenInputY);
};

void PostProcessGenerator::sharpen(Func sharpenInputY) {
    blur(blurOutput, blurOutputTmp, sharpenInputY);
    blur2(blurOutput2, blurOutput2Tmp, blurOutput);
    
    Func gaussianDiff0{"gaussianDiff0"}, gaussianDiff1{"gaussianDiff1"};
    
    gaussianDiff0(v_x, v_y) = cast<int32_t>(sharpenInputY(v_x, v_y)) - blurOutput(v_x, v_y);
    gaussianDiff1(v_x, v_y) = cast<int32_t>(blurOutput(v_x, v_y))  - blurOutput2(v_x, v_y);
    
    Func m{"m"}, n{"n"};

    m(v_x, v_y) = abs(cast<float>(gaussianDiff0(v_x, v_y)) / sharpenThreshold);
    n(v_x, v_y) = abs(cast<float>(gaussianDiff1(v_x, v_y)) / sharpenThreshold);

    RDom r(-2, 2, -2, 2);

    Func M{"M"}, N{"N"};

    M(v_x, v_y) = 1.0f/25.0f * sum(m(v_x + r.x, v_y + r.y));
    N(v_x, v_y) = 1.0f/25.0f * sum(n(v_x + r.x, v_y + r.y));

    Func S{"S"}, T{"T"};

    S(v_x, v_y) = sharpen0 - (sharpen0 - 1.0f)*exp(-M(v_x, v_y));
    T(v_x, v_y) = sharpen1 - (sharpen1 - 1.0f)*exp(-N(v_x, v_y));

    sharpened(v_x, v_y) =
        saturating_cast<int32_t>(
            blurOutput2(v_x, v_y) +
            S(v_x, v_y)*gaussianDiff0(v_x, v_y) +
            T(v_x, v_y)*gaussianDiff1(v_x, v_y)
        );
}

void PostProcessGenerator::generate()
{
    Expr shadowsParam  = shadows;
    Expr blacksParam   = blacks;
    Expr exposureParam = pow(2.0f, exposure);
    Expr satParam      = saturation;

    clamped0 = BoundaryConditions::repeat_edge(in0);
    clamped1 = BoundaryConditions::repeat_edge(in1);
    clamped2 = BoundaryConditions::repeat_edge(in2);
    clamped3 = BoundaryConditions::repeat_edge(in3);
        
    linearScale(shadingMap0, inShadingMap0, inShadingMap0.width(), inShadingMap0.height(), in0.width(), in0.height());
    linearScale(shadingMap1, inShadingMap1, inShadingMap1.width(), inShadingMap1.height(), in1.width(), in1.height());
    linearScale(shadingMap2, inShadingMap2, inShadingMap2.width(), inShadingMap2.height(), in2.width(), in2.height());
    linearScale(shadingMap3, inShadingMap3, inShadingMap3.width(), inShadingMap3.height(), in3.width(), in3.height());

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
        select( v_c == 0, clamp( linear(v_x, v_y, 0), 0.0f, asShotVector[0] ),
                v_c == 1, clamp( linear(v_x, v_y, 1), 0.0f, asShotVector[1] ),
                          clamp( linear(v_x, v_y, 2), 0.0f, asShotVector[2] ));

    Func XYZ{"XYZ"};

    transform(XYZ, colorCorrectInput, cameraToPcs);

    colorCorrected(v_x, v_y, v_c) = select(
            v_c == 0, XYZ(v_x, v_y, 0) / max(1e-5f, XYZ(v_x, v_y, 0) + XYZ(v_x, v_y, 1) + XYZ(v_x, v_y, 2)),
            v_c == 1, XYZ(v_x, v_y, 1) / max(1e-5f, XYZ(v_x, v_y, 0) + XYZ(v_x, v_y, 1) + XYZ(v_x, v_y, 2)),
                      XYZ(v_x, v_y, 1));

    // Blend in highlights
    Func hdrMask32{"hdrMask32"}, hdrInput32{"hdrInput32"}, hdrMerged{"hdrMerged"};

    hdrMask32(v_x, v_y) = BoundaryConditions::repeat_edge(hdrMask)(v_x, v_y) / 255.0f;
    hdrInput32(v_x, v_y, v_c) = BoundaryConditions::repeat_edge(hdrInput)(v_x, v_y, v_c) / 65535.0f;

    hdrMerged(v_x, v_y, v_c) = select(
        v_c == 0, (1.0f - hdrMask32(v_x, v_y))*colorCorrected(v_x, v_y, v_c) + (hdrMask32(v_x, v_y)*hdrInput32(v_x, v_y, v_c)),
        v_c == 1, (1.0f - hdrMask32(v_x, v_y))*colorCorrected(v_x, v_y, v_c) + (hdrMask32(v_x, v_y)*hdrInput32(v_x, v_y, v_c)),
                  exposureParam * (1.0f - hdrMask32(v_x, v_y))*(hdrScale * colorCorrected(v_x, v_y, v_c)) + (hdrMask32(v_x, v_y)*hdrInput32(v_x, v_y, v_c)));

    colorCorrectedYuv(v_x, v_y, v_c) = cast<uint16_t>(clamp(hdrMerged(v_x, v_y, v_c) * 65535 + 0.5f, 0, 65535));

    Func x{"x"}, y{"y"}, Y{"Y"};

    x(v_x, v_y) = colorCorrectedYuv(v_x, v_y, 0);
    y(v_x, v_y) = colorCorrectedYuv(v_x, v_y, 1);
    Y(v_x, v_y) = colorCorrectedYuv(v_x, v_y, 2);

    auto gf0 = create<GuidedFilter>();
    auto gf1 = create<GuidedFilter>();

    gf0->radius.set(31);
    gf0->apply(x, chromaEps*chromaEps*65535.0f*65535.0f);

    gf1->radius.set(31);
    gf1->apply(y, chromaEps*chromaEps*65535.0f*65535.0f);

    tonemap = create<TonemapGenerator>();

    tonemap->output_type.set(UInt(16));
    tonemap->tonemap_levels.set(TONEMAP_LEVELS);
    tonemap->apply(Y, in0.width() * 2, in0.height() * 2, tonemapVariance, gamma, shadowsParam);

    //
    // Sharpen
    //

    // Apply contrast curve while still in XYZ space to avoid exaggerating colours
    Func contrastCurve{"contrastCurve"};

    {
        Expr b = 2.0f - pow(2.0f, contrast);
        Expr a = 2.0f - 2.0f * b;

        // Apply a piecewise quadratic contrast curve
        Expr g = pow(v_i / 65535.0f, 1.0f / gamma);

        Expr h = select(g > 0.5f,
                        1.0f - (a*(1.0f-g)*(1.0f-g) + b*(1.0f-g)),
                        a*g*g + b*g);

        Expr i = pow(h, gamma);

        Func contrastLut{"contrastLut"};

        contrastLut(v_i) = cast<uint16_t>(clamp(i*65535.0f+0.5f, 0.0f, 65535.0f));
        contrastLut.compute_root().vectorize(v_i, 8);

        contrastCurve(v_x, v_y) = contrastLut(cast<uint16_t>(tonemap->output(v_x, v_y)));
    }

    sharpen(contrastCurve);

    finalTonemap(v_x, v_y, v_c) = select(v_c == 0, gf0->output(v_x, v_y) / 65535.0f,
                                         v_c == 1, gf1->output(v_x, v_y) / 65535.0f,
                                                   sharpened(v_x, v_y) / 65535.0f);

    Func tonemappedXYZ{"XYZ"};

    // xyY -> XYZ
    tonemappedXYZ(v_x, v_y, v_c) = select(
        v_c == 0, (finalTonemap(v_x, v_y, 0)*finalTonemap(v_x, v_y, 2)) / finalTonemap(v_x, v_y, 1),
        v_c == 1, finalTonemap(v_x, v_y, 2),
                  ((1.0f - finalTonemap(v_x, v_y, 0) - finalTonemap(v_x, v_y, 1)) * finalTonemap(v_x, v_y, 2)) / finalTonemap(v_x, v_y, 1)
        );

    // To sRGB
    transform(tonemapOutputRgb, tonemappedXYZ, pcsToSrgb);
    
    //
    // Adjust hue & saturation
    //

    rgbToHsv(hsvInput, tonemapOutputRgb);

    shiftHues(saturationApplied, hsvInput, blues, greens, satParam);

    hsvToBgr(finalRgb, saturationApplied);

    // Gamma correct
    Expr g = pow(v_i / 65535.0f, 1.0f / gamma);

    // Apply blacks/white point
    Expr h = (g - blacksParam) / whitePoint;

    gammaLut(v_i) = cast<uint16_t>(clamp(h*65535.0f+0.5f, 0.0f, 65535.0f));

    if(!auto_schedule) {
        if(get_target().has_gpu_feature())
            gammaLut.compute_root().gpu_tile(v_i, v_xi, 16);
        else
            gammaLut.compute_root().vectorize(v_i, 8);
    }

    // Gamma/contrast/black adjustment
    gammaCorrected(v_x, v_y, v_c) = gammaLut(cast<uint16_t>(clamp(finalRgb(v_x, v_y, v_c) * 65535.0f + 0.5f, 0.0f, 65535.0f))) / 65535.0f;
        
    //
    // Finalize output
    //
    
    output(v_x, v_y, v_c) = cast<uint8_t>(clamp(gammaCorrected(v_x, v_y, v_c) * 255 + 0.5f, 0, 255));

    // Output interleaved
    output
        .dim(0).set_stride(3)
        .dim(2).set_stride(1);
    
    range.set_estimate(32767);
    sensorArrangement.set_estimate(0);

    gamma.set_estimate(2.2f);
    contrast.set_estimate(1.5f);
    shadows.set_estimate(2.0f);
    tonemapVariance.set_estimate(0.25f);
    blacks.set_estimate(0.1f);
    exposure.set_estimate(0.05f);
    whitePoint.set_estimate(0.95f);
    blues.set_estimate(1.0f);
    saturation.set_estimate(1.0f);
    greens.set_estimate(1.0f);
    sharpen0.set_estimate(2.0f);
    sharpen1.set_estimate(2.0f);
    chromaEps.set_estimate(0.01f);
    sharpenThreshold.set_estimate(32.0f);
    
    cameraToPcs.set_estimates({{0, 3}, {0, 3}});
    pcsToSrgb.set_estimates({{0, 3}, {0, 3}});

    in0.set_estimates({{0, 2048}, {0, 1536}});
    in1.set_estimates({{0, 2048}, {0, 1536}});
    in2.set_estimates({{0, 2048}, {0, 1536}});
    in3.set_estimates({{0, 2048}, {0, 1536}});

    hdrInput.set_estimates({{0, 4000}, {0, 3000}, {0, 3}});
    hdrMask.set_estimates({{0, 4000}, {0, 3000}});
    hdrScale.set_estimate(2.0f);

    inShadingMap0.set_estimates({{0, 17}, {0, 13}});
    inShadingMap1.set_estimates({{0, 17}, {0, 13}});
    inShadingMap2.set_estimates({{0, 17}, {0, 13}});
    inShadingMap3.set_estimates({{0, 17}, {0, 13}});

    asShotVector.set_estimate(0, 1.0f);
    asShotVector.set_estimate(1, 1.0f);
    asShotVector.set_estimate(2, 1.0f);

    output.set_estimates({{0, 4000}, {0, 3000}, {0, 3}});

    if(!auto_schedule) {
        if(get_target().has_gpu_feature())
            schedule_for_gpu();
        else
            schedule_for_cpu();
    }
}

void PostProcessGenerator::schedule_for_gpu() {
}

void PostProcessGenerator::schedule_for_cpu() { 
    int vector_size_u8 = natural_vector_size<uint8_t>();
    int vector_size_u16 = natural_vector_size<uint16_t>();
    int vector_size_u32 = natural_vector_size<uint32_t>();

    shaded
        .reorder(v_c, v_x, v_y)
        .unroll(v_c)
        .compute_at(combinedInput, v_yo)
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

    demosaic->greenIntermediate
        .compute_at(colorCorrectedYuv, subtile_idx)
        .store_at(colorCorrectedYuv, tile_idx)
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

    // Udownsampled
    //     .compute_root()
    //     .parallel(v_y, 32)
    //     .vectorize(v_x, vector_size_u16);

    // Vdownsampled
    //     .compute_root()
    //     .parallel(v_y, 32)
    //     .vectorize(v_x, vector_size_u16);

    blurOutputTmp
        .compute_at(blurOutput, v_yi)
        .store_at(blurOutput, v_yo)
        .vectorize(v_x, vector_size_u32);

    blurOutput
        .compute_root()
        .reorder(v_x, v_y)
        .split(v_y, v_yo, v_yi, 32)
        .parallel(v_yo)
        .vectorize(v_x, 8);

    blurOutput2Tmp
        .compute_at(blurOutput2, v_yi)
        .store_at(blurOutput2, v_yo)
        .vectorize(v_x, vector_size_u32);

    blurOutput2
        .compute_root()
        .reorder(v_x, v_y)
        .split(v_y, v_yo, v_yi, 32)
        .parallel(v_yo)
        .vectorize(v_x, 8);

    sharpened
        .compute_at(output, v_yi)
        .vectorize(v_x, vector_size_u16);

    finalRgb
        .compute_at(output, v_yi)
        .reorder(v_c, v_x, v_y)
        .unroll(v_c)
        .store_at(output, v_yo)
        .vectorize(v_x, vector_size_u16);

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
    GeneratorParam<int> downscaleFactor{"downscale_factor", 1};

    Input<Buffer<uint8_t>> input{"input", 1};

    Input<Buffer<float>> inShadingMap0{"inShadingMap0", 2 };
    Input<Buffer<float>> inShadingMap1{"inShadingMap1", 2 };
    Input<Buffer<float>> inShadingMap2{"inShadingMap2", 2 };
    Input<Buffer<float>> inShadingMap3{"inShadingMap3", 2 };
    
    Input<float[3]> asShotVector{"asShotVector"};
    Input<Buffer<float>> cameraToPcs{"cameraToPcs", 2};
    Input<Buffer<float>> pcsToSrgb{"pcsToSrgb", 2};

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
    Input<float> blues{"blues"};
    Input<float> greens{"greens"};
    Input<float> saturation{"saturation"};
    Input<float> sharpen0{"sharpen0"};
    Input<float> sharpen1{"sharpen1"};

    Input<bool> flipped{"flipped"};

    Output<Buffer<uint8_t>> output{"output", 3};
    
    std::unique_ptr<TonemapGenerator> tonemap;
    
    void generate();
    void schedule_for_gpu();
    void schedule_for_cpu();
    
private:
    Func downscale(Func f, Func& downx, Expr factor);

private:
    Func inputRepeated{"inputRepeated"};
    Func in[4];
    Func shadingMap[4];
    Func deinterleaved{"deinterleaved"};
    Func downscaled{"downscaled"};
    Func downscaledTemp{"downscaledTemp"};
    Func demosaicInput{"demosaicInput"};
    Func downscaledInput{"downscaledInput"};
    Func adjustExposure{"adjustExposure"};
    Func yuvOutput{"yuvOutput"};
    Func colorCorrected{"colorCorrected"};
    Func colorCorrectedYuv{"colorCorrectedYuv"};
    Func sharpened{"sharpened"};
    Func finalTonemap{"finalTonemap"};
    Func blurOutput{"blurOutput"};
    Func blurOutputTmp{"blurOutputTmp"};
    Func blurOutput2{"blurOutput2"};
    Func blurOutput2Tmp{"blurOutput2Tmp"};

    Func tonemapOutputRgb{"tonemapOutputRgb"};
    Func gammaLut{"gammaContrastLut"};
    Func gammaCorrected{"gammaCorrected"};
    Func hsvInput{"hsvInput"};
    Func saturationApplied{"saturationApplied"};
    Func finalRgb{"finalRgb"};
};

Func PreviewGenerator::downscale(Func f, Func& downx, Expr factor) {
    Func in{"downscaleIn"}, downy{"downy"}, result{"downscaled"};
    RDom r(-factor/2, factor+1);

    in(v_x, v_y, v_c) = cast<float>(f(v_x, v_y, v_c));

    downx(v_x, v_y, v_c) = sum(in(v_x * factor + r.x, v_y, v_c)) / (factor + 1);
    downy(v_x, v_y, v_c) = sum(downx(v_x, v_y * factor + r.x, v_c)) / (factor + 1);

    return downy;
}

void PreviewGenerator::generate() {
    Expr sharpen0Param = sharpen0;
    Expr sharpen1Param = sharpen1;
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
    
    Func input("input");

    input(v_x, v_y, v_c) =
        mux(v_c,
            {   in[0](v_x, v_y),
                in[1](v_x, v_y),
                in[2](v_x, v_y),
                in[3](v_x, v_y) });

    downscaled = downscale(input, downscaledTemp, downscaleFactor);

    // Shading map
    linearScale(shadingMap[0], inShadingMap0, inShadingMap0.width(), inShadingMap0.height(), width, height);
    linearScale(shadingMap[1], inShadingMap1, inShadingMap1.width(), inShadingMap1.height(), width, height);
    linearScale(shadingMap[2], inShadingMap2, inShadingMap2.width(), inShadingMap2.height(), width, height);
    linearScale(shadingMap[3], inShadingMap3, inShadingMap3.width(), inShadingMap3.height(), width, height);

    rearrange(demosaicInput, downscaled, sensorArrangement);

    Expr c0 = (demosaicInput(v_x, v_y, 0) - blackLevel[0]) / (cast<float>(whiteLevel - blackLevel[0])) * shadingMap[0](v_x, v_y);
    Expr c1 = (demosaicInput(v_x, v_y, 1) - blackLevel[1]) / (cast<float>(whiteLevel - blackLevel[1])) * shadingMap[1](v_x, v_y);
    Expr c2 = (demosaicInput(v_x, v_y, 2) - blackLevel[2]) / (cast<float>(whiteLevel - blackLevel[2])) * shadingMap[2](v_x, v_y);
    Expr c3 = (demosaicInput(v_x, v_y, 3) - blackLevel[3]) / (cast<float>(whiteLevel - blackLevel[3])) * shadingMap[3](v_x, v_y);
    
    downscaledInput(v_x, v_y, v_c) = select(v_c == 0,  clamp( c0,               0.0f, asShotVector[0] ),
                                            v_c == 1,  clamp( (c1 + c2) / 2,    0.0f, asShotVector[1] ),
                                                       clamp( c3,               0.0f, asShotVector[2] ));

    Func XYZ{"XYZ"};

    transform(XYZ, downscaledInput, cameraToPcs);

    colorCorrected(v_x, v_y, v_c) = select(
            v_c == 0, XYZ(v_x, v_y, 0) / max(1e-5f, XYZ(v_x, v_y, 0) + XYZ(v_x, v_y, 1) + XYZ(v_x, v_y, 2)),
            v_c == 1, XYZ(v_x, v_y, 1) / max(1e-5f, XYZ(v_x, v_y, 0) + XYZ(v_x, v_y, 1) + XYZ(v_x, v_y, 2)),
                      XYZ(v_x, v_y, 1));

    colorCorrectedYuv(v_x, v_y, v_c) = cast<uint16_t>(clamp(exposureParam * colorCorrected(v_x, v_y, v_c) * 65535.0f + 0.5f, 0, 65535));

    Func x{"x"}, y{"y"}, Y{"Y"};

    x(v_x, v_y) = colorCorrectedYuv(v_x, v_y, 0);
    y(v_x, v_y) = colorCorrectedYuv(v_x, v_y, 1);
    Y(v_x, v_y) = colorCorrectedYuv(v_x, v_y, 2);

    tonemap = create<TonemapGenerator>();

    tonemap->output_type.set(UInt(16));
    tonemap->tonemap_levels.set(tonemap_levels);
    tonemap->apply(Y, width, height, tonemapVariance, gamma, shadowsParam);

    //
    // Sharpen
    //

    // Apply contrast curve while still in XYZ space to avoid exaggerating colours
    Func contrastCurve{"contrastCurve"};

    {
        Expr b = 2.0f - pow(2.0f, contrast);
        Expr a = 2.0f - 2.0f * b;

        // Apply a piecewise quadratic contrast curve
        Expr g = pow(v_i / 65535.0f, 1.0f / gamma);

        Expr h = select(g > 0.5f,
                        1.0f - (a*(1.0f-g)*(1.0f-g) + b*(1.0f-g)),
                        a*g*g + b*g);

        Expr i = pow(h, gamma);

        Func contrastLut{"contrastLut"};

        contrastLut(v_i) = cast<uint16_t>(clamp(i*65535.0f+0.5f, 0.0f, 65535.0f));
        contrastLut.compute_root().vectorize(v_i, 8);

        contrastCurve(v_x, v_y) = contrastLut(cast<uint16_t>(tonemap->output(v_x, v_y)));
    }

    finalTonemap(v_x, v_y, v_c) = select(v_c == 0, x(v_x, v_y),
                                         v_c == 1, y(v_x, v_y),
                                                   contrastCurve(v_x, v_y)) / 65535.0f;

    Func tonemappedXYZ{"XYZ"};

    // xyY -> XYZ
    tonemappedXYZ(v_x, v_y, v_c) = select(
        v_c == 0, (finalTonemap(v_x, v_y, 0)*finalTonemap(v_x, v_y, 2)) / finalTonemap(v_x, v_y, 1),
        v_c == 1, finalTonemap(v_x, v_y, 2),
                  ((1.0f - finalTonemap(v_x, v_y, 0) - finalTonemap(v_x, v_y, 1)) * finalTonemap(v_x, v_y, 2)) / finalTonemap(v_x, v_y, 1)
        );

    // To sRGB
    transform(tonemapOutputRgb, tonemappedXYZ, pcsToSrgb);
    
    //
    // Adjust hue & saturation
    //

    rgbToHsv(hsvInput, tonemapOutputRgb);

    shiftHues(saturationApplied, hsvInput, blues, greens, satParam);

    hsvToBgr(finalRgb, saturationApplied);

    // Gamma correct
    Expr g = pow(v_i / 65535.0f, 1.0f / gamma);

    // Apply blacks/white point
    Expr h = (g - blacksParam) / whitePoint;

    gammaLut(v_i) = cast<uint16_t>(clamp(h*65535.0f+0.5f, 0.0f, 65535.0f));

    if(!auto_schedule) {
        if(get_target().has_gpu_feature())
            gammaLut.compute_root().gpu_tile(v_i, v_xi, 16);
        else
            gammaLut.compute_root().vectorize(v_i, 8);
    }

    // Gamma/contrast/black adjustment
    gammaCorrected(v_x, v_y, v_c) = gammaLut(cast<uint16_t>(clamp(finalRgb(v_x, v_y, v_c) * 65535.0f + 0.5f, 0.0f, 65535.0f))) / 65535.0f;
        
    //
    // Finalize output
    //

    Expr M, N;

    switch(rotation) {
        case 90:
            M = width - v_y;
            N = select(flipped, height - v_x, v_x);
            break;

        case -90:
            M = v_y;
            N = select(flipped, v_x, height - v_x);
            break;

        case 180:
            M = v_x;
            N = height - v_y;
            break;

        default:
        case 0:
            M = select(flipped, width - v_x, v_x);
            N = v_y;
            break;
    }

    output(v_x, v_y, v_c) = cast<uint8_t>(clamp(
        select( v_c == 0, gammaCorrected(M, N, 2) * 255 + 0.5f,
                v_c == 1, gammaCorrected(M, N, 1) * 255 + 0.5f,
                v_c == 2, gammaCorrected(M, N, 0) * 255 + 0.5f,
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
}

void PreviewGenerator::schedule_for_cpu() {
    int vector_size_u8 = natural_vector_size<uint8_t>();
    int vector_size_u16 = natural_vector_size<uint16_t>();    

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
    void apply_auto_schedule(::Halide::Pipeline pipeline, ::Halide::Target target);
};

void DeinterleaveRawGenerator::apply_auto_schedule(::Halide::Pipeline pipeline, ::Halide::Target target) {
    using ::Halide::Func;
    using ::Halide::MemoryType;
    using ::Halide::RVar;
    using ::Halide::TailStrategy;
    using ::Halide::Var;
    Func preview = pipeline.get_func(21);
    Func f9 = pipeline.get_func(20);
    Func output = pipeline.get_func(19);
    Func mirror_image = pipeline.get_func(18);
    Func f4 = pipeline.get_func(17);
    Func f3 = pipeline.get_func(16);
    Func deinterleaveRaw16Result_3 = pipeline.get_func(15);
    Func f8 = pipeline.get_func(14);
    Func deinterleaveRaw10Result_3 = pipeline.get_func(13);
    Func f2 = pipeline.get_func(12);
    Func deinterleaveRaw16Result_2 = pipeline.get_func(11);
    Func f7 = pipeline.get_func(10);
    Func deinterleaveRaw10Result_2 = pipeline.get_func(9);
    Func f1 = pipeline.get_func(8);
    Func deinterleaveRaw16Result_1 = pipeline.get_func(7);
    Func f6 = pipeline.get_func(6);
    Func deinterleaveRaw10Result_1 = pipeline.get_func(5);
    Func f0 = pipeline.get_func(4);
    Func deinterleaveRaw16Result = pipeline.get_func(3);
    Func f5 = pipeline.get_func(2);
    Func deinterleaveRaw10Result = pipeline.get_func(1);
    Var c(output.get_schedule().dims()[2].var);
    Var i(f9.get_schedule().dims()[0].var);
    Var ii("ii");
    Var x(preview.get_schedule().dims()[0].var);
    Var xi("xi");
    Var xii("xii");
    Var xiii("xiii");
    Var y(preview.get_schedule().dims()[1].var);
    Var yi("yi");
    Var yii("yii");
    Var yiii("yiii");
    preview
        .split(y, y, yi, 94, TailStrategy::ShiftInwards)
        .split(x, x, xi, 32, TailStrategy::ShiftInwards)
        .vectorize(xi)
        .compute_root()
        .reorder({xi, x, yi, y})
        .parallel(y);
    f9
        .split(i, i, ii, 32, TailStrategy::RoundUp)
        .vectorize(ii)
        .compute_root()
        .reorder({ii, i})
        .parallel(i);
    output
        .split(y, y, yi, 47, TailStrategy::ShiftInwards)
        .split(x, x, xi, 16, TailStrategy::ShiftInwards)
        .vectorize(xi)
        .compute_root()
        .reorder({xi, x, yi, c, y})
        .parallel(y);
    f4
        .split(y, y, yi, 94, TailStrategy::ShiftInwards)
        .split(yi, yi, yii, 32, TailStrategy::ShiftInwards)
        .split(yii, yii, yiii, 4, TailStrategy::ShiftInwards)
        .split(x, x, xi, 1008, TailStrategy::ShiftInwards)
        .split(xi, xi, xii, 128, TailStrategy::ShiftInwards)
        .split(xii, xii, xiii, 16, TailStrategy::ShiftInwards)
        .vectorize(xiii)
        .compute_root()
        .reorder({xiii, xii, c, xi, x, yiii, yii, yi, y})
        .parallel(y);
    f3
        .split(y, y, yi, 16, TailStrategy::ShiftInwards)
        .split(x, x, xi, 64, TailStrategy::ShiftInwards)
        .split(xi, xi, xii, 16, TailStrategy::ShiftInwards)
        .unroll(xi)
        .vectorize(xii)
        .compute_at(f4, yi)
        .reorder({xii, xi, yi, x, y});
    deinterleaveRaw16Result_3
        .store_in(MemoryType::Stack)
        .split(x, x, xi, 8, TailStrategy::RoundUp)
        .unroll(x)
        .vectorize(xi)
        .compute_at(f3, yi)
        .store_at(f3, x)
        .reorder({xi, x, y});
    f8
        .store_in(MemoryType::Stack)
        .split(x, x, xi, 32, TailStrategy::ShiftInwards)
        .vectorize(xi)
        .compute_at(f3, yi)
        .reorder({xi, x});
    deinterleaveRaw10Result_3
        .split(x, x, xi, 32, TailStrategy::ShiftInwards)
        .vectorize(xi)
        .compute_at(f3, y)
        .reorder({xi, x, y});
    f2
        .store_in(MemoryType::Stack)
        .split(x, x, xi, 512, TailStrategy::ShiftInwards)
        .split(xi, xi, xii, 64, TailStrategy::ShiftInwards)
        .split(xii, xii, xiii, 16, TailStrategy::ShiftInwards)
        .unroll(xii)
        .vectorize(xiii)
        .compute_at(f4, x)
        .reorder({xiii, xii, xi, x, y});
    deinterleaveRaw16Result_2
        .store_in(MemoryType::Stack)
        .split(x, x, xi, 8, TailStrategy::RoundUp)
        .unroll(x)
        .vectorize(xi)
        .compute_at(f2, xi)
        .reorder({xi, x, y});
    f7
        .store_in(MemoryType::Stack)
        .split(x, x, xi, 32, TailStrategy::ShiftInwards)
        .vectorize(xi)
        .compute_at(f2, x)
        .reorder({xi, x});
    deinterleaveRaw10Result_2
        .store_in(MemoryType::Stack)
        .split(x, x, xi, 32, TailStrategy::ShiftInwards)
        .vectorize(xi)
        .compute_at(f4, yii)
        .reorder({xi, x, y});
    f1
        .store_in(MemoryType::Stack)
        .split(x, x, xi, 512, TailStrategy::ShiftInwards)
        .split(xi, xi, xii, 64, TailStrategy::ShiftInwards)
        .split(xii, xii, xiii, 16, TailStrategy::ShiftInwards)
        .unroll(xii)
        .vectorize(xiii)
        .compute_at(f4, x)
        .reorder({xiii, xii, xi, x, y});
    deinterleaveRaw16Result_1
        .store_in(MemoryType::Stack)
        .split(x, x, xi, 8, TailStrategy::RoundUp)
        .unroll(x)
        .vectorize(xi)
        .compute_at(f1, xi)
        .reorder({xi, x, y});
    f6
        .store_in(MemoryType::Stack)
        .split(x, x, xi, 32, TailStrategy::ShiftInwards)
        .vectorize(xi)
        .compute_at(f1, x)
        .reorder({xi, x});
    deinterleaveRaw10Result_1
        .store_in(MemoryType::Stack)
        .split(x, x, xi, 32, TailStrategy::ShiftInwards)
        .vectorize(xi)
        .compute_at(f4, yii)
        .reorder({xi, x, y});
    f0
        .store_in(MemoryType::Stack)
        .split(x, x, xi, 64, TailStrategy::RoundUp)
        .split(xi, xi, xii, 16, TailStrategy::RoundUp)
        .unroll(xi)
        .vectorize(xii)
        .compute_at(f4, xi)
        .reorder({xii, xi, x, y});
    deinterleaveRaw16Result
        .store_in(MemoryType::Stack)
        .split(x, x, xi, 8, TailStrategy::RoundUp)
        .unroll(x)
        .vectorize(xi)
        .compute_at(f0, x)
        .reorder({xi, x, y});
    f5
        .store_in(MemoryType::Stack)
        .split(x, x, xi, 32, TailStrategy::ShiftInwards)
        .vectorize(xi)
        .compute_at(f4, x)
        .reorder({xi, x});
    deinterleaveRaw10Result
        .store_in(MemoryType::Stack)
        .split(x, x, xi, 32, TailStrategy::ShiftInwards)
        .vectorize(xi)
        .compute_at(f4, yii)
        .reorder({xi, x, y});

}

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

    Expr P = 0.25f * (clamped(x, y, 0) +
                      clamped(x, y, 1) +
                      clamped(x, y, 2) +
                      clamped(x, y, 3));

    Expr S = (P - blackLevel[0]) / (whiteLevel - blackLevel[0]);

    preview(v_x, v_y) =  gammaLut(cast<uint8_t>(clamp(S * scale * 255.0f + 0.5f, 0, 255)));

    input.set_estimates({ {0, 18000000} });
    width.set_estimate(4000);
    height.set_estimate(3000);
    blackLevel.set_estimate(0, 64);
    blackLevel.set_estimate(1, 64);
    blackLevel.set_estimate(2, 64);
    blackLevel.set_estimate(3, 64);
    whiteLevel.set_estimate(1023);
    offsetX.set_estimate(0);
    offsetY.set_estimate(0);
    scale.set_estimate(1.0f);
    stride.set_estimate(4000);
    sensorArrangement.set_estimate(0);
    pixelFormat.set_estimate(0);

    output.set_estimates({{0, 2000}, {0, 1500}, {0, 4} });
    preview.set_estimates({{0, 2000}, {0, 1500} });

    if(!get_auto_schedule()) {
        //schedule_for_cpu();
        apply_auto_schedule(get_pipeline(), get_target());
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

    Input<float[3]> asShotVector{"asShotVector"};
    Input<Buffer<float>> cameraToSrgb{"cameraToSrgb", 2};

    Input<Buffer<float>[4]> inShadingMap{"shadingMap", 2};

    Input<int> sensorArrangement{"sensorArrangement"};

    Output<Buffer<uint32_t>> histogram{"histogram", 1};

    void generate();
};

void MeasureImageGenerator::generate() {
    Func inputRepeated{"inputRepeated"};
    Func in[4];
    Func shadingMap[4];
    Func downscaled{"downscaled"};
    Func result8u{"result8u"};
    Func colorCorrected{"colorCorrected"};
    Func downscaledInput{"downscaledInput"};
    Func demosaicInput{"demosaicInput"};

    // Deinterleave
    inputRepeated = BoundaryConditions::repeat_edge(input);

    // Deinterleave
    deinterleave(in[0], inputRepeated, 0, stride, pixelFormat);
    deinterleave(in[1], inputRepeated, 1, stride, pixelFormat);
    deinterleave(in[2], inputRepeated, 2, stride, pixelFormat);
    deinterleave(in[3], inputRepeated, 3, stride, pixelFormat);

    Expr w = width / downscaleFactor;
    Expr h = height / downscaleFactor;
    
    downscaled(v_x, v_y, v_c) =
        mux(v_c,
            {   in[0](v_x*downscaleFactor, v_y*downscaleFactor),
                in[1](v_x*downscaleFactor, v_y*downscaleFactor),
                in[2](v_x*downscaleFactor, v_y*downscaleFactor),
                in[3](v_x*downscaleFactor, v_y*downscaleFactor) });

    // Shading map
    linearScale(shadingMap[0], inShadingMap[0], inShadingMap[0].width(), inShadingMap[0].height(), w, h);
    linearScale(shadingMap[1], inShadingMap[1], inShadingMap[1].width(), inShadingMap[1].height(), w, h);
    linearScale(shadingMap[2], inShadingMap[2], inShadingMap[2].width(), inShadingMap[2].height(), w, h);
    linearScale(shadingMap[3], inShadingMap[3], inShadingMap[3].width(), inShadingMap[3].height(), w, h);

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

    Expr L = 0.2989f*colorCorrected(v_x, v_y, 0) + 0.5870f*colorCorrected(v_x, v_y, 1) + 0.1140f*colorCorrected(v_x, v_y, 2);

    result8u(v_x, v_y) = cast<uint8_t>(clamp(L * 255 + 0.5f, 0, 255));

    RDom r(0, w, 0, h);

    histogram(v_i) = cast<uint32_t>(0);
    histogram(result8u(r.x, r.y)) += cast<uint32_t>(1);

    // Schedule
    result8u
        .compute_root()
        .reorder(v_x, v_y)
        .parallel(v_y, 32)
        .vectorize(v_x, 8);

    histogram
        .compute_root()
        .vectorize(v_i, 32);
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
    Func channel[4], channel32;

    deinterleave(channel[0], input, 0, stride, pixelFormat);
    deinterleave(channel[1], input, 1, stride, pixelFormat);
    deinterleave(channel[2], input, 2, stride, pixelFormat);
    deinterleave(channel[3], input, 3, stride, pixelFormat);
    
    channel32(v_x, v_y) =
        cast<int32_t>(channel[0](v_x, v_y)) +
        cast<int32_t>(channel[1](v_x, v_y)) +
        cast<int32_t>(channel[2](v_x, v_y)) +
        cast<int32_t>(channel[3](v_x, v_y));

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

//////////////

class HdrMaskGenerator : public Halide::Generator<HdrMaskGenerator> {
public:
    Input<Buffer<uint8_t>> input0{"input0", 2};
    Input<Buffer<uint8_t>> input1{"input1", 2};    

    Input<float> c{"c"};

    Output<Buffer<uint8_t>> outputGhost{"outputGhost", 2};
    Output<Buffer<uint8_t>> outputMask{"outputMask", 2};

    void generate();

private:
    void schedule_for_cpu(::Halide::Pipeline pipeline, ::Halide::Target target);

    Var v_x{"x"};
    Var v_y{"y"};
    Var v_yo{"yo"};
    Var v_yi{"yi"};
};

void HdrMaskGenerator::generate() {
    Func inputf0, inputf1;
    Func mask0, mask1;
    Func map0, map1;
    Func ghostMap;

    inputf0(v_x, v_y) = max(0.0f, min(1.0f, cast<float>(BoundaryConditions::repeat_edge(input0)(v_x, v_y)) / 255.0f));
    inputf1(v_x, v_y) = max(0.0f, min(1.0f, cast<float>(BoundaryConditions::repeat_edge(input1)(v_x, v_y)) / 255.0f));

    mask0(v_x, v_y) = exp(-c * (inputf0(v_x, v_y) - 1.0f) * (inputf0(v_x, v_y) - 1.0f));
    mask1(v_x, v_y) = exp(-c * (inputf1(v_x, v_y) - 1.0f) * (inputf1(v_x, v_y) - 1.0f));

    map0(v_x, v_y) = cast<uint8_t>(select(mask0(v_x, v_y) > 0.5f, 1, 0));
    map1(v_x, v_y) = cast<uint8_t>(select(mask1(v_x, v_y) > 0.5f, 1, 0));

    ghostMap(v_x, v_y) = map0(v_x, v_y) ^ map1(v_x, v_y);

    RDom r(-3, 3, -3, 3);

    outputGhost(v_x, v_y) = cast<uint8_t>(1);
    outputGhost(v_x, v_y) = outputGhost(v_x, v_y) & ghostMap(v_x + r.x, v_y + r.y);
    
    outputMask(v_x, v_y) = cast<uint8_t>(clamp(mask0(v_x, v_y) * 255.0f + 0.5f, 0, 255));

    c.set_estimate(4.0f);

    input0.set_estimates({{0, 2048}, {0, 1536}});
    input1.set_estimates({{0, 2048}, {0, 1536}});

    outputGhost.set_estimates({{0, 2048}, {0, 1536}});
    outputMask.set_estimates({{0, 2048}, {0, 1536}});

    if(!auto_schedule) {
        schedule_for_cpu(get_pipeline(), get_target());
    }
}

void HdrMaskGenerator::schedule_for_cpu(::Halide::Pipeline pipeline, ::Halide::Target target) {
    using ::Halide::Func;
    using ::Halide::MemoryType;
    using ::Halide::RVar;
    using ::Halide::TailStrategy;
    using ::Halide::Var;
    Var x_i("x_i");
    Var x_i_vi("x_i_vi");
    Var x_i_vo("x_i_vo");
    Var x_o("x_o");
    Var x_vi("x_vi");
    Var x_vo("x_vo");
    Var y_i("y_i");
    Var y_o("y_o");

    Func f6 = pipeline.get_func(12);
    Func outputGhost = pipeline.get_func(13);
    Func outputMask = pipeline.get_func(14);

    {
        Var x = f6.args()[0];
        f6
            .compute_at(outputGhost, x_o)
            .split(x, x_vo, x_vi, 32)
            .vectorize(x_vi);
    }
    {
        Var x = outputGhost.args()[0];
        Var y = outputGhost.args()[1];
        RVar r30$x(outputGhost.update(0).get_schedule().rvars()[0].var);
        RVar r30$y(outputGhost.update(0).get_schedule().rvars()[1].var);
        outputGhost
            .compute_root()
            .split(x, x_vo, x_vi, 32)
            .vectorize(x_vi)
            .parallel(y);
        outputGhost.update(0)
            .reorder(r30$x, x, r30$y, y)
            .split(x, x_o, x_i, 256, TailStrategy::GuardWithIf)
            .split(y, y_o, y_i, 256, TailStrategy::GuardWithIf)
            .reorder(r30$x, x_i, r30$y, y_i, x_o, y_o)
            .split(x_i, x_i_vo, x_i_vi, 32, TailStrategy::GuardWithIf)
            .vectorize(x_i_vi)
            .parallel(y_o)
            .parallel(x_o);
    }
    {
        Var x = outputMask.args()[0];
        Var y = outputMask.args()[1];
        outputMask
            .compute_root()
            .split(x, x_vo, x_vi, 32)
            .vectorize(x_vi)
            .parallel(y);
    }

}

//////////////

class LinearImageGenerator : public Halide::Generator<LinearImageGenerator>, public PostProcessBase {
public:
    Input<Buffer<uint16_t>> in0{"in0", 2 };
    Input<Buffer<uint16_t>> in1{"in1", 2 };
    Input<Buffer<uint16_t>> in2{"in2", 2 };
    Input<Buffer<uint16_t>> in3{"in3", 2 };

    Input<Buffer<float>> inShadingMap0{"inShadingMap0", 2 };
    Input<Buffer<float>> inShadingMap1{"inShadingMap1", 2 };
    Input<Buffer<float>> inShadingMap2{"inShadingMap2", 2 };
    Input<Buffer<float>> inShadingMap3{"inShadingMap3", 2 };

    Input<float[3]> asShotVector{"asShotVector"};    
    Input<Buffer<float>> cameraToPcs{"cameraToPcs", 2};

    Input<int> width{"width"};
    Input<int> height{"height"};

    Input<int> sensorArrangement{"sensorArrangement"};
    
    Input<int16_t[4]> blackLevel{"blackLevel"};
    Input<int16_t> whiteLevel{"whiteLevel"};
    Input<float> whitePoint{"whitePoint"};

    Output<Buffer<uint16_t>> output{"output", 3};

    void generate();

private:
    void schedule_for_cpu(::Halide::Pipeline pipeline, ::Halide::Target target);

    std::unique_ptr<Demosaic> demosaic;
    Func shadingMap[4];
    Func downscaled{"downscaled"};
    Func shaded{"shaded"};
    Func colorCorrected{"colorCorrected"};
    Func combinedInput{"combinedInput"};
    Func mirroredInput{"mirroredInput"};
    Func bayerInput{"bayerInput"}; 
};

void LinearImageGenerator::generate() {
    Func in[4];

    in[0] = BoundaryConditions::repeat_edge(in0);
    in[1] = BoundaryConditions::repeat_edge(in1);
    in[2] = BoundaryConditions::repeat_edge(in2);
    in[3] = BoundaryConditions::repeat_edge(in3);

    downscaled(v_x, v_y, v_c) =
        mux(v_c,
            {  in[0](v_x, v_y),
               in[1](v_x, v_y),
               in[2](v_x, v_y),
               in[3](v_x, v_y) });

    // Shading map
    linearScale(shadingMap[0], inShadingMap0, inShadingMap0.width(), inShadingMap0.height(), width, height);
    linearScale(shadingMap[1], inShadingMap1, inShadingMap1.width(), inShadingMap1.height(), width, height);
    linearScale(shadingMap[2], inShadingMap2, inShadingMap2.width(), inShadingMap2.height(), width, height);
    linearScale(shadingMap[3], inShadingMap3, inShadingMap3.width(), inShadingMap3.height(), width, height);

    Func shadingMapArranged{"shadingMapArranged"};

    rearrange(shadingMapArranged, shadingMap[0], shadingMap[1], shadingMap[2], shadingMap[3], sensorArrangement);

    shaded(v_x, v_y, v_c) = 
        select( v_c == 0, cast<int16_t>( clamp( (downscaled(v_x, v_y, v_c) - blackLevel[0]) / (cast<float>(whiteLevel - blackLevel[0])) * shadingMapArranged(v_x, v_y, 0) * 16384.0f, 0, 16384.0f) ),
                v_c == 1, cast<int16_t>( clamp( (downscaled(v_x, v_y, v_c) - blackLevel[1]) / (cast<float>(whiteLevel - blackLevel[1])) * shadingMapArranged(v_x, v_y, 1) * 16384.0f, 0, 16384.0f) ),
                v_c == 2, cast<int16_t>( clamp( (downscaled(v_x, v_y, v_c) - blackLevel[2]) / (cast<float>(whiteLevel - blackLevel[2])) * shadingMapArranged(v_x, v_y, 2) * 16384.0f, 0, 16384.0f) ),
                          cast<int16_t>( clamp( (downscaled(v_x, v_y, v_c) - blackLevel[3]) / (cast<float>(whiteLevel - blackLevel[3])) * shadingMapArranged(v_x, v_y, 3) * 16384.0f, 0, 16384.0f) ) );

    // Demosaic image
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
    
    Func linear("linear"), colorCorrectInput("colorCorrectInput");

    linear(v_x, v_y, v_c) = (demosaic->output(v_x, v_y, v_c) / 16384.0f) * whitePoint;

    colorCorrectInput(v_x, v_y, v_c) =
        select( v_c == 0, clamp( linear(v_x, v_y, 0), 0.0f, asShotVector[0] ),
                v_c == 1, clamp( linear(v_x, v_y, 1), 0.0f, asShotVector[1] ),
                          clamp( linear(v_x, v_y, 2), 0.0f, asShotVector[2] ));
    
    transform(colorCorrected, colorCorrectInput, cameraToPcs);

    Expr X = colorCorrected(v_x, v_y, 0);
    Expr Y = colorCorrected(v_x, v_y, 1);
    Expr Z = colorCorrected(v_x, v_y, 2);

    output(v_x, v_y, v_c) = cast<uint16_t>(
        clamp(
            select(
                v_c == 0, X / max(1e-5f, X + Y + Z) * 65535.0f + 0.5f,
                v_c == 1, Y / max(1e-5f, X + Y + Z) * 65535.0f + 0.5f,
                          Y * 65535.0f + 0.5f),
            0, 65535)
    );

    in0.set_estimates({{0, 2048}, {0, 1536}});
    in1.set_estimates({{0, 2048}, {0, 1536}});
    in2.set_estimates({{0, 2048}, {0, 1536}});
    in3.set_estimates({{0, 2048}, {0, 1536}});

    inShadingMap0.set_estimates({{0, 17}, {0, 13}});
    inShadingMap1.set_estimates({{0, 17}, {0, 13}});
    inShadingMap2.set_estimates({{0, 17}, {0, 13}});
    inShadingMap3.set_estimates({{0, 17}, {0, 13}});

    asShotVector.set_estimate(0, 1.0f);
    asShotVector.set_estimate(1, 1.0f);
    asShotVector.set_estimate(2, 1.0f);

    cameraToPcs.set_estimates({{0, 3}, {0, 3}});
    sensorArrangement.set_estimate(0);

    blackLevel.set_estimate(0, 64);
    blackLevel.set_estimate(1, 64);
    blackLevel.set_estimate(2, 64);
    blackLevel.set_estimate(3, 64);
    whiteLevel.set_estimate(1023);
    whitePoint.set_estimate(1.0f);
    width.set_estimate(2048);
    height.set_estimate(1536);

    output.set_estimates({{0, 4096}, {0, 3072}, {0, 3}});

    if(!auto_schedule)
        schedule_for_cpu(get_pipeline(), get_target());
}

void LinearImageGenerator::schedule_for_cpu(::Halide::Pipeline pipeline, ::Halide::Target target) {
    using ::Halide::Func;
    using ::Halide::MemoryType;
    using ::Halide::RVar;
    using ::Halide::TailStrategy;
    using ::Halide::Var;
    Var x_i("x_i");
    Var x_i_vi("x_i_vi");
    Var x_i_vo("x_i_vo");
    Var x_o("x_o");
    Var x_vi("x_vi");
    Var x_vo("x_vo");
    Var y_i("y_i");
    Var y_o("y_o");

    Func bayerInput = pipeline.get_func(26);
    Func blue = pipeline.get_func(32);
    Func f0 = pipeline.get_func(15);
    Func f1 = pipeline.get_func(17);
    Func f12 = pipeline.get_func(34);
    Func f16 = pipeline.get_func(30);
    Func f2 = pipeline.get_func(19);
    Func f3 = pipeline.get_func(21);
    Func green = pipeline.get_func(28);
    Func greenIntermediate = pipeline.get_func(27);
    Func mirror_image = pipeline.get_func(25);
    Func output = pipeline.get_func(41);
    Func red = pipeline.get_func(36);

    {
        Var x = bayerInput.args()[0];
        bayerInput
            .compute_at(output, x_o)
            .split(x, x_vo, x_vi, 16)
            .vectorize(x_vi);
    }
    {
        Var x = blue.args()[0];
        blue
            .compute_at(output, x_o)
            .split(x, x_vo, x_vi, 16)
            .vectorize(x_vi);
    }
    {
        Var x = f0.args()[0];
        Var y = f0.args()[1];
        f0
            .compute_root()
            .split(x, x_vo, x_vi, 8)
            .vectorize(x_vi)
            .parallel(y);
    }
    {
        Var x = f1.args()[0];
        Var y = f1.args()[1];
        f1
            .compute_root()
            .split(x, x_vo, x_vi, 8)
            .vectorize(x_vi)
            .parallel(y);
    }
    {
        Var x = f12.args()[0];
        f12
            .compute_at(output, x_o)
            .split(x, x_vo, x_vi, 8)
            .vectorize(x_vi);
    }
    {
        Var x = f16.args()[0];
        f16
            .compute_at(output, x_o)
            .split(x, x_vo, x_vi, 8)
            .vectorize(x_vi);
    }
    {
        Var x = f2.args()[0];
        Var y = f2.args()[1];
        f2
            .compute_root()
            .split(x, x_vo, x_vi, 8)
            .vectorize(x_vi)
            .parallel(y);
    }
    {
        Var x = f3.args()[0];
        Var y = f3.args()[1];
        f3
            .compute_root()
            .split(x, x_vo, x_vi, 8)
            .vectorize(x_vi)
            .parallel(y);
    }
    {
        Var x = green.args()[0];
        green
            .compute_at(output, x_o)
            .split(x, x_vo, x_vi, 16)
            .vectorize(x_vi);
    }
    {
        Var x = greenIntermediate.args()[0];
        greenIntermediate
            .compute_at(output, x_o)
            .split(x, x_vo, x_vi, 16)
            .vectorize(x_vi);
    }
    {
        Var x = mirror_image.args()[0];
        mirror_image
            .compute_at(output, x_o)
            .split(x, x_vo, x_vi, 16)
            .vectorize(x_vi);
    }
    {
        Var x = output.args()[0];
        Var y = output.args()[1];
        Var c = output.args()[2];
        output
            .compute_root()
            .split(x, x_o, x_i, 256)
            .split(y, y_o, y_i, 256)
            .reorder(x_i, y_i, c, x_o, y_o)
            .split(x_i, x_i_vo, x_i_vi, 16)
            .vectorize(x_i_vi)
            .parallel(y_o)
            .parallel(x_o);
    }
    {
        Var x = red.args()[0];
        red
            .compute_at(output, x_o)
            .split(x, x_vo, x_vi, 16)
            .vectorize(x_vi);
    }
}

//////////////

HALIDE_REGISTER_GENERATOR(GenerateEdgesGenerator, generate_edges_generator)
HALIDE_REGISTER_GENERATOR(MeasureImageGenerator, measure_image_generator)
HALIDE_REGISTER_GENERATOR(DeinterleaveRawGenerator, deinterleave_raw_generator)
HALIDE_REGISTER_GENERATOR(PostProcessGenerator, postprocess_generator)
HALIDE_REGISTER_GENERATOR(PreviewGenerator, preview_generator)
HALIDE_REGISTER_GENERATOR(HdrMaskGenerator, hdr_mask_generator)
HALIDE_REGISTER_GENERATOR(LinearImageGenerator, linear_image_generator)

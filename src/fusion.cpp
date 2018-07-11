/*******************************************************************************
 *
 * MIT License
 *
 * Copyright (c) 2017 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 *******************************************************************************/
#include <cassert>
#include <miopen/fusion.hpp>
#include <miopen/logger.hpp>
#include <miopen/handle.hpp>
#include <miopen/visit_float.hpp>

namespace miopen {

miopenStatus_t FusionPlanDescriptor::AddOp(std::shared_ptr<FusionOpDescriptor> desc)
{
    desc->SetIdx(op_count);
    if(op_map.empty())
        desc->SetInputDesc(input_desc);
    else
        desc->SetInputDesc(output_desc);
    desc->GetOutputDesc(output_desc);
    op_map.emplace_back(desc);
    op_count++;
    is_valid = lu.Advance(op_map);
    return miopenStatusSuccess;
}

TensorDescriptor FusionPlanDescriptor::DeriveOutputDescriptor()
{
    TensorDescriptor i_desc = input_desc;
    TensorDescriptor o_desc;
    if(fusion_dir == miopenVerticalFusion)
    {
        for(auto&& op : op_map)
        {
            op->SetInputDesc(i_desc);
            op->GetOutputDesc(o_desc);
            i_desc = o_desc;
        }
    }
    else
    {
        // TODO: All the ops should have the same output descriptor otherwise
        // fusion would not be feasible, thus we need to call GetOutputDesc on all
        // the ops and make sure it returns the same value
        MIOPEN_THROW("Unsupported fusion direction");
    }
    return o_desc;
}

miopenStatus_t FusionPlanDescriptor::GetWorkspaceSizeImmed(Handle& handle,
                                                           size_t& workSpaceSize,
                                                           miopenConvFwdAlgorithm_t algo)
{
    workSpaceSize = 0;
    // iterate over all the conv ops in the plan and return the max amount of
    // ws required
    for(auto&& op : op_map)
    {
        if(op->kind() == miopenFusionOpConvForward)
        {
            auto ptr = std::dynamic_pointer_cast<ConvForwardOpDescriptor>(op);
            TensorDescriptor opd;
            ptr->GetOutputDesc(opd);
            bool supported = false;
            size_t tmp_sz  = ptr->base_desc.ForwardGetWorkSpaceSizeImmed(
                handle, ptr->filter_desc, ptr->input_desc, opd, algo, supported);
            if(supported && (tmp_sz > workSpaceSize))
                workSpaceSize = tmp_sz;
        }
    }
    return miopenStatusSuccess;
}

std::ostream& operator<<(std::ostream& stream, const FusionPlanDescriptor& fpd)
{
    (void)(fpd);
    /*    MIOPEN_LOG_ENUM(stream,
                        x.mode,
                        miopenActivationPASTHRU,
                        miopenActivationLOGISTIC,
                        miopenActivationTANH,
                        miopenActivationRELU,
                        miopenActivationSOFTRELU,
                        miopenActivationABS,
                        miopenActivationPOWER,
                        miopenActivationCLIPPEDRELU,
                        miopenActivationLEAKYRELU,
                        miopenActivationELU)*/
    // LogRange(stream, x.parms, ", ") << ", ";
    return stream;
}

// Fusion operator descriptors
// Conv Forward
miopenStatus_t ConvForwardOpDescriptor::GetOutputDesc(TensorDescriptor& output_desc)
{
    std::size_t n, c, h, w;
    std::tie(n, c, h, w) = base_desc.GetForwardOutputDim(input_desc, filter_desc);
    TensorDescriptor desc(input_desc.GetType(), {n, c, h, w});
    output_desc = desc;
    return miopenStatusSuccess;
}

miopenStatus_t ConvForwardOpDescriptor::SetArgs(OperatorArgs& args,
                                                const void* alpha,
                                                const void* beta,
                                                ConstData_t w)
{
    (void)(alpha);
    (void)(beta);
    // const float* f_alpha = static_cast<const float*>(alpha);
    auto id = std::to_string(GetIdx());
    // args.ins_arg("alpha" + id, any_t(*f_alpha));
    // args.ins_arg("beta" + id, any_t(*(static_cast<const float*>(beta))));
    auto w_any = any_t(w);
    args.ins_arg("weights" + id, w_any);

    return miopenStatusSuccess;
}

std::vector<std::string> ConvForwardOpDescriptor::GetArgs() const
{
    std::vector<std::string> keys;
    keys.push_back("weights" + std::to_string(GetIdx()));
    return keys;
}

// Activ Forward
miopenStatus_t ActivFusionOpDescriptor::SetArgs(OperatorArgs& args,
                                                const void* alpha,
                                                const void* beta,
                                                double activAlpha,
                                                double activBeta,
                                                double activGamma)
{
    auto id             = std::to_string(GetIdx());
    auto alpha_any      = any_t(*(static_cast<const float*>(alpha)));
    auto beta_any       = any_t(*(static_cast<const float*>(beta)));
    auto activAlpha_any = any_t(static_cast<float>(activAlpha));
    auto activBeta_any  = any_t(static_cast<float>(activBeta));
    auto activGamma_any = any_t(static_cast<float>(activGamma));
    // args.ins_arg("alpha" + id, alpha_any);
    // args.ins_arg("beta" + id, beta_any);
    args.ins_arg("activAlpha" + id, activAlpha_any);
    args.ins_arg("activBeta" + id, activBeta_any);
    args.ins_arg("activGamma" + id, activGamma_any);
    return miopenStatusSuccess;
}

std::vector<std::string> ActivFusionOpDescriptor::GetArgs() const
{
    std::vector<std::string> keys;
    auto id = std::to_string(GetIdx());
    // keys.push_back("alpha" + id);
    // keys.push_back("beta" + id);
    keys.push_back("activAlpha" + id);
    keys.push_back("activBeta" + id);
    keys.push_back("activGamma" + id);
    return keys;
}

miopenStatus_t ActivFusionOpDescriptor::GetOutputDesc(TensorDescriptor& output_desc)
{
    // activation does not change the size
    output_desc = input_desc;
    return miopenStatusSuccess;
}

miopenStatus_t BatchNormInferenceFusionOpDescriptor::GetOutputDesc(TensorDescriptor& output_desc)
{
    output_desc = input_desc;
    return miopenStatusSuccess;
}

miopenStatus_t BatchNormInferenceFusionOpDescriptor::SetArgs(OperatorArgs& args,
                                                             const void* alpha,
                                                             const void* beta,
                                                             ConstData_t bnScale,
                                                             ConstData_t bnBias,
                                                             ConstData_t estimatedMean,
                                                             ConstData_t estimatedVariance,
                                                             double epsilon)
{
    auto id                    = std::to_string(GetIdx());
    auto alpha_any             = any_t(*(static_cast<const float*>(alpha)));
    auto beta_any              = any_t(*(static_cast<const float*>(beta)));
    auto bnScale_any           = any_t(bnScale);
    auto bnBias_any            = any_t(bnBias);
    auto estimatedMean_any     = any_t(estimatedMean);
    auto estimatedVariance_any = any_t(estimatedVariance);
    auto epsilon_any           = any_t(static_cast<double>(epsilon));
    args.ins_arg("epsilon" + id, epsilon_any);
    args.ins_arg("bnScale" + id, bnScale_any);
    args.ins_arg("bnBias" + id, bnBias_any);
    args.ins_arg("estimatedMean" + id, estimatedMean_any);
    args.ins_arg("estimatedVariance" + id, estimatedVariance_any);
    return miopenStatusSuccess;
}

std::vector<std::string> BatchNormInferenceFusionOpDescriptor::GetArgs() const
{
    std::vector<std::string> keys;
    auto id = std::to_string(GetIdx());
    // keys.push_back("alpha" + id);
    // keys.push_back("beta" + id);
    keys.push_back("epsilon" + id);
    keys.push_back("bnScale" + id);
    keys.push_back("bnBias" + id);
    keys.push_back("estimatedMean" + id);
    keys.push_back("estimatedVariance" + id);
    return keys;
}

// Bias forward
miopenStatus_t BiasFusionOpDescriptor::GetOutputDesc(TensorDescriptor& output_desc)
{
    output_desc = input_desc;
    return miopenStatusSuccess;
}

miopenStatus_t BiasFusionOpDescriptor::SetArgs(OperatorArgs& args,
                                               const void* alpha,
                                               const void* beta,
                                               ConstData_t bdata)
{
    auto id = std::to_string(GetIdx());
    (void)(alpha);
    (void)(beta);
    //    args.ins_arg("alpha" + id, any_t(*static_cast<const float*>(alpha)));
    //    args.ins_arg("beta" + id, any_t(*static_cast<const float*>(beta)));
    auto bdata_any = any_t(bdata);
    args.ins_arg("bias" + id, bdata_any);
    return miopenStatusSuccess;
}

std::vector<std::string> BiasFusionOpDescriptor::GetArgs() const
{
    std::vector<std::string> keys;
    keys.push_back("bias" + std::to_string(GetIdx()));
    return keys;
}
// Op LUT
bool FusionOpLU::Advance(std::vector<std::shared_ptr<miopen::FusionOpDescriptor>> op_map)
{

    auto valid = false;
    for(auto supportedOps : lut)
    {
        valid = std::equal(supportedOps.begin(),
                           supportedOps.begin() + op_map.size() - 1,
                           op_map.begin(),
                           [&](miopenFusionOp_t x, std::shared_ptr<miopen::FusionOpDescriptor> y) {
                               return x == y->kind();
                           });
        if(valid)
            return valid;
    }
    /*    if(valid)
        {
            cur_idx = idx + 1;
            lut_hit.push_back(1);
            return valid;
        }
        else
            lut_hit.push_back(0);

        lut_hit.resize(cur_idx_tmp);*/
    return false;
}

/*auto FusionOpLU::GetPaths()
{
    std::vector<miopenFusionOp_t> vec;
    for(size_t idx = 0; lut_hit.size(); idx++)
    {
        if(lut_hit[idx] == 1)
            vec.push_back(lut[idx]);
    }
    return vec;
}
*/

std::string FusionPlanDescriptor::GetProgramName(Handle& handle)
{

    auto starting_op = op_map.at(0);

    if(starting_op->kind() == miopenFusionOpConvForward)
    {
        if(fp_contains_bn)
        {
            program_name = "MIOpenConvDirBatchNormActiv.cl";
        }
        else
        {
            auto ki =
                std::dynamic_pointer_cast<ConvForwardOpDescriptor>(starting_op)->GetKernelInfo(handle);
            program_name = ki.kernel_file;

        }
        return program_name;
    }
    else if(starting_op->kind() == miopenFusionOpBatchNormInference)
    {
        // This is hardcoded to assume that batch norm is first AND that
        // activations is the next fusion. Currently no other fusion exists
        // where BN is the op_head.
        program_name = "MIOpenBatchNormActivInfer.cl";
        return program_name;
    }
    else
    {
        MIOPEN_THROW("Unsupported starting op in Fusion Plan");
    }
}

std::string FusionPlanDescriptor::GetKernelName(Handle& handle)
{
    auto ops_head = op_map.at(0);
    if(ops_head->kind() == miopenFusionOpConvForward)
    {
        if(fp_contains_bn)
        {
            kernel_name = "MIOpenConvUniBatchNormActiv";
        }
        else
        {
            auto ki =
                std::dynamic_pointer_cast<ConvForwardOpDescriptor>(ops_head)->GetKernelInfo(handle);
            kernel_name = ki.kernel_name;
        }
        
        return kernel_name;
    }
    else if(ops_head->kind() == miopenFusionOpBatchNormInference)
    {
        // This is hardcoded to assume that batch norm is first AND that
        // activations is the next fusion. Currently no other fusion exists
        // where BN is the op_head.
        auto bnOp =
            std::dynamic_pointer_cast<miopen::BatchNormInferenceFusionOpDescriptor>(ops_head);
        auto bn_mode = bnOp->mode;
        if(bn_mode == miopenBNSpatial)
        {
            kernel_name = "MIOpenBatchNormActivInferSpatialEst";
            
        }
        else
        {
            kernel_name = "MIOpenBatchNormActivInferPerActEst";
        }
        return kernel_name;
    }
    else
    {
        MIOPEN_THROW("Unsupported starting op in Fusion Plan");
    }
}

miopenStatus_t FusionPlanDescriptor::Compile(Handle& handle)
{
    miopenStatus_t status = miopenStatusUnknownError;
    if(!isValid())
    {
        MIOPEN_THROW("Trying to compile and invalid FusionPlan");
    }
    // std::string network_config{};
    network_config = "";
    program_name = "";
    kernel_name = "";
/*    std::string program_name{};
    std::string kernel_name{};*/
    // TODO: move the hard coded algo name to the LUT
    // std::string algorithm_name{}; // = "miopenDirConvBatchNormActivAlgo";
    // TODO: The fusion plan is keeping track of the insertion order,
    // should we move this to the Graph ?
    for(auto&& op : op_map)
    {
        op->GetNetworkConfig(network_config, handle);
    }
    // Check if the kernel is assembly or OpenCL
    is_asm_kernel       = false;
    fp_contains_bn = false;
    // TODO: The Metadata graph should return this info
    auto ops_head = op_map[0]; // ins_order[0]];
    for(auto&& op : op_map)
    { // This needs to go away with the meta graph.
        if(op->kind() == miopenFusionOpBatchNormInference)
        {
            printf("Fusion plan contains batch norm.\n");
            fp_contains_bn = true;
            break;
        }
    }
    if(ops_head->kind() == miopenFusionOpConvForward)
    {
        auto ops_conv = std::dynamic_pointer_cast<ConvForwardOpDescriptor>(ops_head);

        if(!fp_contains_bn)
        { // If we get BN asm code then we can do this....
            is_asm_kernel = ops_conv->isASMApplicable(handle);
            if(is_asm_kernel)
            {
                algorithm_name = "miopenConvolutionDirectBiasActivAsm";
            }
            else
            {
                algorithm_name = "miopenConvolutionDirectBiasActiv";
            }
        }
        else
        {
            std::cout << "Is this an assembly kernel? " << is_asm_kernel << std::endl;
            algorithm_name = "miopenConvDirectBatchNormBiasActiv";
        }
    }
    else if(ops_head->kind() == miopenFusionOpBatchNormInference)
    {
        fp_contains_bn = true;
        algorithm_name = "miopenBatchNormActivInferAlgo";
    }
    else
    {
        status = miopenStatusNotImplemented;
    }

    auto&& kernels = handle.GetKernels(algorithm_name, network_config);
    if(!kernels.empty())
    {
        status = miopenStatusSuccess;
    }
    else
    {
        std::string compile_config;
        auto dType = input_desc.GetType();
        if(dType == miopenFloat)
        {
            compile_config += " -DMIOPEN_USE_FP16=0 -DMIOPEN_USE_FP32=1";
        }
        else
        {
            compile_config += " -DMIOPEN_USE_FP16=1 -DMIOPEN_USE_FP32=0";
        }

        if(ops_head->kind() == miopenFusionOpConvForward)
        {

            // DLOWELL: This is a hack to prevent ASM kernels from being called
            auto ki = std::dynamic_pointer_cast<ConvForwardOpDescriptor>(ops_head)->GetKernelInfo(
                handle, algorithm_name);
            // We should use GetKernelName() and GetProgram name at this point.
            /*program_name = ki.kernel_file;
            kernel_name  = ki.kernel_name;*/
            // const auto parms = compile_config;
            const auto& vld = ki.l_wk;
            const auto& vgd = ki.g_wk;
            for(auto&& op : op_map)
            {
                op->GetCompileParms(compile_config, handle, is_asm_kernel);
            }
            std::cout << "Add Kernel Compiler options: " << compile_config << std::endl;
            std::cout << "Program name: " << GetProgramName(handle) << std::endl;
            std::cout << "Kernel name: " <<  GetKernelName(handle) << std::endl;
            handle.AddKernel(algorithm_name,
                             network_config,
                             program_name,
                             kernel_name,
                             vld,
                             vgd,
                             compile_config);
            status = miopenStatusSuccess;
        }
        else if(ops_head->kind() == miopenFusionOpBatchNormInference)
        {

            std::vector<size_t> vld{256, 1, 1};
            // This is hardcoded to assume that batch norm is first AND that
            // activations is the next fusion. Currently no other fusion exists
            // where BN is the op_head.
            // \todo this needs to be dynamic and based on information for a single source.

            int n, c, h, w;

            // The output_desc should be fully formed by this stage.
            std::tie(n, c, h, w) = tien<4>(output_desc.GetLengths());
            printf("n: %d, c: %d, h: %d, w: %d\n", n, c, h, w);
            auto bnOp =
                std::dynamic_pointer_cast<miopen::BatchNormInferenceFusionOpDescriptor>(ops_head);
            auto bn_mode     = bnOp->mode;
            size_t read_unit = 0;
            size_t read_len  = (bn_mode == miopenBNSpatial) ? h * w : c * h * w;

            if(bn_mode /*ops_head->mode*/ == miopenBNSpatial)
            {
                read_unit = (read_len % 4 == 0) ? 4 : (read_len % 2 == 0) ? 2 : 1;
            }
            else
            {
                read_unit = 1;
            }
            compile_config += " -DMIO_BN_CHW=" + std::to_string(c * h * w) + " -DMIO_BN_HW=" +
                              std::to_string(h * w) + " -DMIO_BN_N=" + std::to_string(n) +
                              " -DMIO_BN_GRP0=" + std::to_string(vld.at(0)) + " -DMIO_BN_GRP1=" +
                              std::to_string(1) + " -DMIO_BN_GRP2=" + std::to_string(1);

            printf("read_len: %d, read_unit: %d\n", read_len, read_unit);
            size_t xgridsize = read_len / read_unit;
            size_t ygridsize = (bn_mode == miopenBNSpatial) ? size_t(c) : 1;
            size_t zgridsize = 1;

            std::vector<size_t> vgd{};
            vgd.push_back(xgridsize);
            vgd.push_back(ygridsize);
            vgd.push_back(zgridsize);

            std::cout << "vld: " << vld.at(0) << ", " << vld.at(1) << ", " << vld.at(2)
                      << std::endl;
            std::cout << "vgd: " << vgd.at(0) << ", " << vgd.at(1) << ", " << vgd.at(2)
                      << std::endl;

            std::string READ_TYPE =
                (read_unit == 1) ? "_FLOAT" : "_FLOAT" + std::to_string(read_unit);
            compile_config += " -DMIOPEN_READ_UNIT=" + std::to_string(read_unit);
            compile_config += " -DMIOPEN_READ_TYPE=" + READ_TYPE;
            for(auto&& op : op_map)
            {
                op->GetCompileParms(compile_config, handle, is_asm_kernel);
            }
            std::cout << "Compiler parameters: " << compile_config << std::endl;
            handle.AddKernel(algorithm_name,
                             network_config,
                             GetProgramName(handle),
                             GetKernelName(handle),
                             vld,
                             vgd,
                             compile_config);
            status = miopenStatusSuccess;
        }
        else
        {
            status = miopenStatusNotImplemented;
        }
    }
    return status;
}

miopenStatus_t FusionPlanDescriptor::Execute(Handle& handle,
                                             TensorDescriptor& inputDesc,
                                             ConstData_t input,
                                             TensorDescriptor& outputDesc,
                                             Data_t output,
                                             const OperatorArgs& op_args)
{
    if(!isValid())
    {
        MIOPEN_THROW("Attempting to execute an invalid fusion plan.");
    }

    if(output_desc != outputDesc)
    {
        MIOPEN_THROW("The output descriptors dont match.");
    }
    if(input_desc != inputDesc)
    {
        MIOPEN_THROW("The input descriptors dont match.");
    }

    // TODO: The Metadata graph should return this info
    auto ops_head = op_map[0];

    auto&& kernels = handle.GetKernels(algorithm_name, network_config);
    if(kernels.empty())
    {
        MIOPEN_THROW("The FusionPlan was not compiled for execution");
    }
    KernelInvoke kernel = kernels.front();

    // Construct the kernel args
    std::set<size_t> arg_sizes; // a set of argument sizes
    // A map between argument sizes and argument names
    std::map<std::pair<size_t, size_t>, std::vector<std::string>> size_map;
    // A map between argument pointers (buffers) and argument names
    std::map<size_t, std::vector<std::string>> ptr_map;

    for(auto idx = 0; idx < op_map.size(); idx++)
    {
        auto op   = op_map[idx];
        auto keys = op->GetArgs();
        for(auto&& key : keys)
        {
            auto it = op_args.args_map.find(key);
            if(it != op_args.args_map.end())
            {
                if(!it->second.is_ptr)
                {
                    arg_sizes.insert(it->second.size());
                    size_map[std::pair<size_t, size_t>(idx, it->second.size())].push_back(key);
                }
                else
                {
                    ptr_map[idx].push_back(key);
                }
            }
            else
                MIOPEN_THROW("Arg " + key + " was not set for Operator: " +
                             std::to_string(op->kind()));
        }
    }

    std::vector<any_t> args;
    for(auto sz : arg_sizes) // Populate args for scalars
    {
        for(auto idx = 0; idx < op_map.size(); idx++)
        {
            auto op   = op_map[idx];
            auto keys = size_map[std::pair<size_t, size_t>(idx, sz)];
            std::sort(keys.begin(), keys.end());
            for(auto key : keys)
            {
                std::cout << "Populate scalar args, key: " << key << std::endl;
                auto it = op_args.args_map.find(key);
                if(it != op_args.args_map.end())
                {
                    args.push_back(it->second);
                }
            }
        }
    }
    // insert input / output pointer
    args.emplace_back(any_t(input));
    args.emplace_back(any_t(output));
    // add other pointers in op-order
    for(auto idx = 0; idx < op_map.size(); idx++)
    { // Populate args for pointers based operator order
        auto op   = op_map[idx];
        auto keys = ptr_map[idx];
        std::sort(keys.begin(), keys.end());
        for(auto key : keys)
        {
            std::cout << "Populate arg pointers, key: " << key << std::endl;
            auto it = op_args.args_map.find(key);
            if(it != op_args.args_map.end())
                args.push_back(it->second);
        }
    }
    if(is_asm_kernel)
    { // Padded arguments
        std::vector<any_t> padded_args;
        size_t running_sz = args[0].size();
        padded_args.push_back(std::move(args[0]));
        for(auto idx = 1; idx < args.size(); idx++)
        {
            if(args[idx - 1].size() != args[idx].size())
            {
                auto padding = running_sz % args[idx].size();
                if(padding != 0)
                {
                    printf("*************  Adding padding: %d\n", padding);
                    any_t tmp(0, padding);
                    padded_args.push_back(tmp);
                    running_sz += padding;
                }
            }
            padded_args.push_back(std::move(args[idx]));
            running_sz += args[idx].size();
        }
        kernel(padded_args);
    }
    else
    {
        kernel(args);
    }
    return miopenStatusSuccess;
}

} // namespace miopen

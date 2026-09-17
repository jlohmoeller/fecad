#include "pdta/config/CryptoContextFactory.hpp"

#include "pdta/config/CommandLineParameters.hpp"

namespace pat_disc {
    CryptoContext GenerateBooleanContext(bool forceToy)
    {
        lbcrypto::CCParams<lbcrypto::CryptoContextBFVRNS> parameters;
        parameters.SetMaxRelinSkDeg(3);
        parameters.SetScalingModSize(55);
        parameters.SetPlaintextModulus(k_BFVPlaintextMod);
        parameters.SetMultiplicativeDepth(1 + 2);
        parameters.SetRingDim(k_BFVRingDimension);
        parameters.SetBatchSize(k_BFVBatchSize);

        if (ta::CommandLineParameters::GetInstance().IsUseToyParameters() || forceToy)
        {
            PD_WARN("Running using toy parameters!");
            parameters.SetSecurityLevel(lbcrypto::SecurityLevel::HEStd_NotSet);
        } else
        {
            parameters.SetSecurityLevel(lbcrypto::SecurityLevel::HEStd_128_classic);
        }

        lbcrypto::CryptoContext<lbcrypto::DCRTPoly> cc = GenCryptoContext(parameters);
        cc->Enable(lbcrypto::PKE);
        cc->Enable(lbcrypto::PRE);
        cc->Enable(lbcrypto::LEVELEDSHE);
        cc->Enable(lbcrypto::ADVANCEDSHE);

        return cc;
    }

    CryptoContext GeneratePreciseEnumContext(const bool forceToy)
    {
        const int32_t multiplicativeDepth = static_cast<int32_t>(std::ceil(std::log2(k_BFVPlaintextMod - 1))) + 2;

        lbcrypto::CCParams<lbcrypto::CryptoContextBFVRNS> parameters;
        parameters.SetMaxRelinSkDeg(3);
        parameters.SetScalingModSize(55);
        parameters.SetPlaintextModulus(k_BFVPlaintextMod);
        parameters.SetMultiplicativeDepth(multiplicativeDepth);
        parameters.SetRingDim(k_BFVRingDimension);
        parameters.SetBatchSize(k_BFVBatchSize);

        if (ta::CommandLineParameters::GetInstance().IsUseToyParameters() || forceToy)
        {
            PD_WARN("Running using toy parameters!");
            parameters.SetSecurityLevel(lbcrypto::SecurityLevel::HEStd_NotSet);
        } else
        {
            parameters.SetSecurityLevel(lbcrypto::SecurityLevel::HEStd_128_classic);
        }

        lbcrypto::CryptoContext<lbcrypto::DCRTPoly> cc = GenCryptoContext(parameters);
        cc->Enable(lbcrypto::PKE);
        cc->Enable(lbcrypto::PRE);
        cc->Enable(lbcrypto::LEVELEDSHE);
        cc->Enable(lbcrypto::ADVANCEDSHE);

        return cc;
    }

    CryptoContext GenerateApproxEnumContext(const bool forceToy)
    {
        lbcrypto::CCParams<lbcrypto::CryptoContextCKKSRNS> parameters;

        if (ta::CommandLineParameters::GetInstance().IsLegacySignApproximation())
        {
            parameters.SetMultiplicativeDepth(17);
        } else
        {
            parameters.SetMultiplicativeDepth(13);
        }

        if (ta::CommandLineParameters::GetInstance().IsUseToyParameters() || forceToy)
        {
            PD_WARN("Running using toy parameters!");
            parameters.SetSecurityLevel(lbcrypto::SecurityLevel::HEStd_NotSet);
        } else
        {
            parameters.SetSecurityLevel(lbcrypto::SecurityLevel::HEStd_128_classic);
        }

        parameters.SetRingDim(k_CKKSRingDimension);
        parameters.SetScalingModSize(50);
        parameters.SetBatchSize(k_CKKSBatchSize);

        lbcrypto::CryptoContext<lbcrypto::DCRTPoly> cc = GenCryptoContext(parameters);
        cc->Enable(lbcrypto::PKE);
        cc->Enable(lbcrypto::PRE);
        cc->Enable(lbcrypto::LEVELEDSHE);
        cc->Enable(lbcrypto::ADVANCEDSHE);

        return cc;
    }

    CryptoContext GenerateApproxContinuousContext(const bool forceToy)
    {
        lbcrypto::CCParams<lbcrypto::CryptoContextCKKSRNS> parameters;

        if (ta::CommandLineParameters::GetInstance().IsLegacySignApproximation())
        {
            parameters.SetMultiplicativeDepth(33);
        } else
        {
            parameters.SetMultiplicativeDepth(18);
        }

        if (ta::CommandLineParameters::GetInstance().IsUseToyParameters() || forceToy)
        {
            PD_WARN("Running using toy parameters!");
            parameters.SetSecurityLevel(lbcrypto::SecurityLevel::HEStd_NotSet);
        } else
        {
            parameters.SetSecurityLevel(lbcrypto::SecurityLevel::HEStd_128_classic);
        }

        parameters.SetRingDim(k_CKKSRingDimension);
        parameters.SetScalingModSize(50);
        parameters.SetBatchSize(k_CKKSBatchSize);

        lbcrypto::CryptoContext<lbcrypto::DCRTPoly> cc = GenCryptoContext(parameters);
        cc->Enable(lbcrypto::PKE);
        cc->Enable(lbcrypto::PRE);
        cc->Enable(lbcrypto::LEVELEDSHE);
        cc->Enable(lbcrypto::ADVANCEDSHE);

        return cc;
    }


    CryptoContext GeneratePreciseContinuousContext(const bool forceToy)
    {
        lbcrypto::CryptoContext<lbcrypto::DCRTPoly> cc;
        if (ta::CommandLineParameters::GetInstance().IsLegacyPreciseLessThan())
        {
            lbcrypto::CCParams<lbcrypto::CryptoContextCKKSRNS> parameters;
            // We need 13 for the operation + 2 for query depth
            parameters.SetMultiplicativeDepth(15);
            parameters.SetFirstModSize(60);
            parameters.SetScalingModSize(50);
            parameters.SetScalingTechnique(lbcrypto::FLEXIBLEAUTOEXT);
            parameters.SetBatchSize(k_CKKSSchemeSwitchBatchSize);

            if (ta::CommandLineParameters::GetInstance().IsUseToyParameters() || forceToy)
            {
                PD_WARN("Running using toy parameters!");
                parameters.SetSecurityLevel(lbcrypto::SecurityLevel::HEStd_NotSet);
                parameters.SetRingDim(8192);
            } else
            {
                parameters.SetSecurityLevel(lbcrypto::SecurityLevel::HEStd_128_classic);
            }


            cc = GenCryptoContext(parameters);
        } else
        {
            lbcrypto::CCParams<lbcrypto::CryptoContextBFVRNS> parameters;
            parameters.SetScalingModSize(55);
            parameters.SetPlaintextModulus(k_BFVPlaintextMod);
            parameters.SetMultiplicativeDepth(21);
            parameters.SetRingDim(k_BFVRingDimension);
            parameters.SetBatchSize(k_BFVBatchSize);

            if (ta::CommandLineParameters::GetInstance().IsUseToyParameters() || forceToy)
            {
                PD_WARN("Running using toy parameters!");
                parameters.SetSecurityLevel(lbcrypto::SecurityLevel::HEStd_NotSet);
            } else
            {
                parameters.SetSecurityLevel(lbcrypto::SecurityLevel::HEStd_128_classic);
            }

            cc = GenCryptoContext(parameters);
        }

        cc->Enable(lbcrypto::PKE);
        cc->Enable(lbcrypto::PRE);
        cc->Enable(lbcrypto::LEVELEDSHE);
        cc->Enable(lbcrypto::ADVANCEDSHE);

        if (ta::CommandLineParameters::GetInstance().IsLegacyPreciseLessThan())
        {
            cc->Enable(lbcrypto::SCHEMESWITCH);
        }

        return cc;
    }

    lbcrypto::SchSwchParams GetSchemeSwitchParamsForCompare(const bool forceToy)
    {
        lbcrypto::SchSwchParams params;
        params.SetCtxtModSizeFHEWLargePrec(25);
        params.SetNumSlotsCKKS(k_CKKSSchemeSwitchBatchSize);
        params.SetNumValues(k_CKKSSchemeSwitchBatchSize);

        if (ta::CommandLineParameters::GetInstance().IsUseToyParameters() || forceToy)
        {
            PD_WARN("Running using toy parameters!");
            params.SetSecurityLevelCKKS(lbcrypto::SecurityLevel::HEStd_NotSet);
            params.SetSecurityLevelFHEW(lbcrypto::TOY);
        } else
        {
            params.SetSecurityLevelCKKS(lbcrypto::SecurityLevel::HEStd_128_classic);
            params.SetSecurityLevelFHEW(lbcrypto::STD128);
        }

        return params;
    }

    CryptoContext GeneratePreciseDistanceContext(const bool forceToy)
    {
        lbcrypto::CryptoContext<lbcrypto::DCRTPoly> cc;
        if (ta::CommandLineParameters::GetInstance().IsLegacyPreciseLessThan())
        {
            lbcrypto::CCParams<lbcrypto::CryptoContextCKKSRNS> parameters;
            // We need 14 for the operation + 2 for query depth
            parameters.SetMultiplicativeDepth(16);
            parameters.SetFirstModSize(60);
            parameters.SetScalingModSize(50);
            parameters.SetScalingTechnique(lbcrypto::FLEXIBLEAUTOEXT);
            parameters.SetBatchSize(k_CKKSSchemeSwitchBatchSize);

            if (ta::CommandLineParameters::GetInstance().IsUseToyParameters() || forceToy)
            {
                PD_WARN("Running using toy parameters!");
                parameters.SetSecurityLevel(lbcrypto::SecurityLevel::HEStd_NotSet);
                parameters.SetRingDim(8192);
            } else
            {
                parameters.SetSecurityLevel(lbcrypto::SecurityLevel::HEStd_128_classic);
            }

            cc = GenCryptoContext(parameters);
        } else
        {
            lbcrypto::CCParams<lbcrypto::CryptoContextBFVRNS> parameters;
            parameters.SetScalingModSize(55);
            parameters.SetPlaintextModulus(k_BFVPlaintextMod);
            parameters.SetMultiplicativeDepth(21);
            parameters.SetRingDim(k_BFVRingDimension);
            parameters.SetBatchSize(k_BFVBatchSize);

            if (ta::CommandLineParameters::GetInstance().IsUseToyParameters() || forceToy)
            {
                PD_WARN("Running using toy parameters!");
                parameters.SetSecurityLevel(lbcrypto::SecurityLevel::HEStd_NotSet);
            } else
            {
                parameters.SetSecurityLevel(lbcrypto::SecurityLevel::HEStd_128_classic);
            }

            cc = GenCryptoContext(parameters);
        }

        cc->Enable(lbcrypto::PKE);
        cc->Enable(lbcrypto::PRE);
        cc->Enable(lbcrypto::LEVELEDSHE);
        cc->Enable(lbcrypto::ADVANCEDSHE);

        if (ta::CommandLineParameters::GetInstance().IsLegacyPreciseLessThan())
        {
            cc->Enable(lbcrypto::SCHEMESWITCH);
        }

        return cc;
    }

    CryptoContext GenerateApproxDistanceContext(const bool forceToy)
    {
        lbcrypto::CCParams<lbcrypto::CryptoContextCKKSRNS> parameters;

        if (ta::CommandLineParameters::GetInstance().IsLegacySignApproximation())
        {
            parameters.SetMultiplicativeDepth(48);
        } else
        {
            parameters.SetMultiplicativeDepth(25);
        }

        if (ta::CommandLineParameters::GetInstance().IsUseToyParameters() || forceToy)
        {
            PD_WARN("Running using toy parameters!");
            parameters.SetSecurityLevel(lbcrypto::SecurityLevel::HEStd_NotSet);
        } else
        {
            parameters.SetSecurityLevel(lbcrypto::SecurityLevel::HEStd_128_classic);
        }

        parameters.SetRingDim(k_CKKSRingDimension);
        parameters.SetScalingModSize(50);
        parameters.SetBatchSize(k_CKKSBatchSize);

        lbcrypto::CryptoContext<lbcrypto::DCRTPoly> cc = GenCryptoContext(parameters);
        cc->Enable(lbcrypto::PKE);
        cc->Enable(lbcrypto::PRE);
        cc->Enable(lbcrypto::ADVANCEDSHE);
        cc->Enable(lbcrypto::LEVELEDSHE);

        return cc;
    }
}

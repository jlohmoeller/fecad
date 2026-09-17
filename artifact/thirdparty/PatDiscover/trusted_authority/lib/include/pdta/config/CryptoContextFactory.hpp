#ifndef SERVER_CRYPTOCONTEXTFACTORY_HPP
#define SERVER_CRYPTOCONTEXTFACTORY_HPP

namespace pat_disc {
    CryptoContext GenerateBooleanContext(bool forceToy = false);

    CryptoContext GeneratePreciseEnumContext(bool forceToy = false);

    CryptoContext GenerateApproxEnumContext(bool forceToy = false);

    CryptoContext GeneratePreciseContinuousContext(bool forceToy = false);

    lbcrypto::SchSwchParams GetSchemeSwitchParamsForCompare(bool forceToy = false);

    CryptoContext GenerateApproxContinuousContext(bool forceToy = false);

    CryptoContext GeneratePreciseDistanceContext(bool forceToy = false);

    CryptoContext GenerateApproxDistanceContext(bool forceToy = false);
}

#endif //SERVER_CRYPTOCONTEXTFACTORY_HPP

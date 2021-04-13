#ifndef ENJINCPPSDK_PROJECTSETTRANSFERFEE_HPP
#define ENJINCPPSDK_PROJECTSETTRANSFERFEE_HPP

#include "enjinsdk_export.h"
#include "enjinsdk/internal/AbstractGraphqlRequest.hpp"
#include "enjinsdk/shared/TransactionRequestArguments.hpp"
#include <optional>
#include <string>

namespace enjin::sdk::project {

/// \brief Request for setting the transfer fee of a asset.
class ENJINSDK_EXPORT SetTransferFee : public graphql::AbstractGraphqlRequest,
                                       public shared::TransactionRequestArguments<SetTransferFee> {
public:
    /// \brief Default constructor.
    SetTransferFee();

    ~SetTransferFee() override = default;

    [[nodiscard]] std::string serialize() const override;

    /// \brief Sets the asset ID.
    /// \param asset_id The ID.
    /// \return This request for chaining.
    SetTransferFee& set_asset_id(const std::string& asset_id);

    /// \brief Sets the index for non-fungible assets.
    /// \param asset_index The index.
    /// \return This request for chaining.
    SetTransferFee& set_asset_index(const std::string& asset_index);

    /// \brief Sets the new transfer fee value in Wei.
    /// \param transfer_fee The new transfer fee.
    /// \return This request for chaining.
    SetTransferFee& set_transfer_fee(const std::string& transfer_fee);

    bool operator==(const SetTransferFee& rhs) const;

    bool operator!=(const SetTransferFee& rhs) const;

private:
    std::optional<std::string> asset_id;
    std::optional<std::string> asset_index;
    std::optional<std::string> transfer_fee;
};

}

namespace enjin::sdk::shared {

template ENJINSDK_EXPORT project::SetTransferFee&
TransactionFragmentArguments<project::SetTransferFee>::set_with_blockchain_data();

template ENJINSDK_EXPORT project::SetTransferFee&
TransactionFragmentArguments<project::SetTransferFee>::set_with_meta();

template ENJINSDK_EXPORT project::SetTransferFee&
TransactionFragmentArguments<project::SetTransferFee>::set_with_encoded_data();

template ENJINSDK_EXPORT project::SetTransferFee&
TransactionFragmentArguments<project::SetTransferFee>::set_with_asset_data();

template ENJINSDK_EXPORT project::SetTransferFee&
TransactionFragmentArguments<project::SetTransferFee>::set_with_signed_txs();

template ENJINSDK_EXPORT project::SetTransferFee&
TransactionFragmentArguments<project::SetTransferFee>::set_with_error();

template ENJINSDK_EXPORT project::SetTransferFee&
TransactionFragmentArguments<project::SetTransferFee>::set_with_nonce();

template ENJINSDK_EXPORT project::SetTransferFee&
TransactionFragmentArguments<project::SetTransferFee>::set_with_state();

template ENJINSDK_EXPORT project::SetTransferFee&
TransactionFragmentArguments<project::SetTransferFee>::set_with_receipt();

template ENJINSDK_EXPORT project::SetTransferFee&
TransactionFragmentArguments<project::SetTransferFee>::set_with_receipt_logs();

template ENJINSDK_EXPORT project::SetTransferFee&
TransactionFragmentArguments<project::SetTransferFee>::set_with_log_event();

template ENJINSDK_EXPORT project::SetTransferFee&
TransactionRequestArguments<project::SetTransferFee>::set_eth_address(const std::string& address);

template ENJINSDK_EXPORT project::SetTransferFee&
TransactionRequestArguments<project::SetTransferFee>::set_send(bool send);

}

#endif //ENJINCPPSDK_PROJECTSETTRANSFERFEE_HPP

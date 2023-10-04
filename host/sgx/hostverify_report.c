// Copyright (c) Open Enclave SDK contributors.
// Licensed under the MIT License.

#include <openenclave/bits/report.h>
#include <openenclave/bits/result.h>
#include <openenclave/host.h>
#include <openenclave/host_verify.h>
#include <openenclave/internal/raise.h>

#include "../../common/sgx/quote.h"
#include "sgxquoteprovider.h"

oe_result_t oe_verify_remote_report(
    const uint8_t* report,
    size_t report_size,
    const uint8_t* endorsement,
    size_t endorsement_size,
    oe_report_t* parsed_report)
{
    oe_result_t result = OE_UNEXPECTED;
    oe_report_t oe_report = {0};
    oe_report_header_t* header = (oe_report_header_t*)report;
    uint32_t verification_result = 0;

    if (report == NULL)
        OE_RAISE(OE_INVALID_PARAMETER);

    if (report_size == 0 || report_size > OE_MAX_REPORT_SIZE)
        OE_RAISE(OE_INVALID_PARAMETER);

    // The two host side attestation API's are oe_get_report and
    // oe_verify_report. Initialize the quote provider in both these APIs.
    OE_CHECK(oe_initialize_quote_provider());

    // Ensure that the report is parseable before using the header.
    OE_CHECK(oe_parse_report(report, report_size, &oe_report));

    if (header->report_type != OE_REPORT_TYPE_SGX_REMOTE)
        OE_RAISE(OE_UNSUPPORTED);

    // Quote attestation can be done entirely on the host side.
    result = oe_verify_sgx_quote(
        header->report,
        header->report_size,
        endorsement,
        endorsement_size,
        NULL,
        &verification_result);

    // Optionally return parsed report and set the verification result
    if (parsed_report != NULL)
    {
        *parsed_report = oe_report;
        parsed_report->verification_result = verification_result;
    }

done:
    return result;
}
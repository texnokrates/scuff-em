/*
 * ScuffCLI.h -- internal helpers for typed CLI11 applications.
 *
 * This header is intentionally not part of the installed public interface:
 * no public SCUFF-EM header should expose CLI11.  Applications that use it
 * link the private scuff_cli11 target.
 */

#ifndef SCUFFCLI_H
#define SCUFFCLI_H

#include <complex>
#include <optional>
#include <string>
#include <vector>

#include <CLI11.hpp>

namespace scuff {
namespace cli {

/***************************************************************/
/* Build the normalized token vector for CLI11 from argv plus  */
/* the legacy stdin option-file format.  The returned vector   */
/* is already in the order expected by CLI::App::parse.        */
/***************************************************************/
std::vector<std::string> CollectArgs(int argc, char *argv[]);

/***************************************************************/
/* Add a complex-valued option parsed with the legacy S2CD     */
/* syntax.  CLI11's built-in converter splits at the last '+'  */
/* or '-' and therefore rejects valid values such as           */
/* 1e-3-4e-2i; S2CD handles the full syntax, including         */
/* uppercase I/J.  Repeated occurrences follow the legacy      */
/* scalar-option rule: the last one wins.                      */
/***************************************************************/
CLI::Option *AddComplexOption(CLI::App &App, const std::string &Name,
                              std::complex<double> &Value,
                              const std::string &Description);

/***************************************************************/
/* Add a repeatable complex-valued option whose values         */
/* accumulate in order of appearance, parsed with the legacy   */
/* S2CD syntax.  This reproduces the legacy PA_CDOUBLE options */
/* with MaxInstances>1 (for example scuff-scatter's --Omega    */
/* and --Lambda); MaxInstances caps the number of occurrences  */
/* and NumArgs values are consumed per occurrence (legacy      */
/* PA_CDOUBLE with NumArgs>1, e.g. --psStrength).              */
/***************************************************************/
CLI::Option *AddComplexVectorOption(CLI::App &App, const std::string &Name,
                                    std::vector<std::complex<double>> &Values,
                                    const std::string &Description,
                                    int MaxInstances = 1000000,
                                    int NumArgs = 1);

/***************************************************************/
/* Parse App from argv and stdin.  Returns std::nullopt when   */
/* parsing succeeded and the caller should continue; otherwise */
/* returns the process exit status: 0 for help/version, 1 for  */
/* an error (matching the legacy option parser).               */
/***************************************************************/
std::optional<int> Parse(CLI::App &App, int argc, char *argv[]);

} // namespace cli
} // namespace scuff

#endif

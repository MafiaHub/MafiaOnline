# 1930 price references

Amounts here are nominal US dollars of the time, with no modern inflation conversion.
Lost Heaven is fictional, so national figures are anchors, not evidence for the
price of a particular building or district.

| Reference                                 | Period figure | Source and interpretation                                                                                                                                                                                                                                                                                                                                    |
| ----------------------------------------- | ------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Median owned nonfarm home value           | $4,778        | [1930 Census, Population Volume VI, introduction, p. 7, Table 5](https://www2.census.gov/library/publications/decennial/1930/population-volume-6/41129380v6ch01.pdf). Owner-reported value, not a sale listing or a new-construction price. Used as the default house price; admins override individual properties.                                          |
| Median monthly rent, nonfarm rented homes | $27.16        | Same Census table. Useful context for a future rental system; rent is not implemented here.                                                                                                                                                                                                                                                                  |
| Manufacturing hourly earnings             | $0.589        | [NBER, _Wages, Hours, and Employment in the United States, 1914–1936_, December 1936, Table 8](https://www.nber.org/sites/default/files/2020-02/dec21_1936.pdf), 1930 column. Industry-specific, not an average for every person.                                                                                                                            |
| Lumber workers' full-time weekly earnings | $20.28        | [BLS Bulletin 560, _Wages and Hours of Labor in the Lumber Industry in the United States, 1930_](https://fraser.stlouisfed.org/title/wages-hours-labor-lumber-manufacturing-3915/wages-hours-labor-lumber-industry-united-states-1930-493159/fulltext). Surveyed male sawmill workers, with a 56.5-hour full-time week. Actual work and unemployment varied. |

The provisional $25 starting wallet is a gameplay choice of roughly a week's
industrial pay, not a measured 1930 savings balance. It is set by the account
migration/default in `src/server/accounts.ts`. Existing accounts gain the same
initial persisted balance when upgrading from the earlier account-only system.
House prices can be set with `/house_set`; console `setmoney` sets account balances.

Before adding jobs and everyday purchases, introduce an explicit cents-based money
contract and decide how game time maps to work/pay periods. The current whole-dollar
native balance is enough for house purchases, but cannot represent a 59-cent wage
or inexpensive goods accurately. Do not multiply historical prices by a modern
inflation factor or silently speed up wages: those are distinct gameplay choices.

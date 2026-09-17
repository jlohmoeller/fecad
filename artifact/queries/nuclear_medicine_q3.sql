-- Q3: Combined (5 attributes)
-- Adult high-grade GBM patients with MGMT methylation and IDH wild-type
SELECT * FROM data WHERE whoGrade >= 3 AND tumorType = 0 AND mgmtPromoterMethylation = 1 AND idhWildType = 1 AND age >= 18

export function TableWrapper({
  children,
  className,
}: {
  children: React.ReactNode;
  className?: string;
}) {
  return (
    <section className={`flow table-wrapper ${className}`}>{children}</section>
  );
}
